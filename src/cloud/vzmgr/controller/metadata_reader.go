package controller

import (
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/gogo/protobuf/proto"
	"github.com/gogo/protobuf/types"
	"github.com/jmoiron/sqlx"
	"github.com/nats-io/nats.go"
	"github.com/nats-io/stan.go"
	uuid "github.com/satori/go.uuid"
	log "github.com/sirupsen/logrus"

	"pixielabs.ai/pixielabs/src/cloud/shared/messages"
	messagespb "pixielabs.ai/pixielabs/src/cloud/shared/messagespb"
	"pixielabs.ai/pixielabs/src/cloud/shared/vzshard"
	"pixielabs.ai/pixielabs/src/shared/cvmsgspb"
	metadatapb "pixielabs.ai/pixielabs/src/shared/k8s/metadatapb"
	"pixielabs.ai/pixielabs/src/utils"
)

// The topic on which to make metadata requests.
const metadataRequestTopic = "MetadataRequest"

// The topic on which to listen to metadata responses.
const metadataResponseTopic = "MetadataResponse"

// The topic on which to listen for streaming metadata updates.
const streamingMetadataTopic = "DurableMetadataUpdates"

// The topic on which to write updates to.
const indexerMetadataTopic = "MetadataIndex"

// VizierState contains all state necessary to process metadata updates for the given vizier.
type VizierState struct {
	id            uuid.UUID // The Vizier's ID.
	updateVersion int64     // The Vizier's last applied resource version.
	k8sUID        string    // The Vizier's K8s UID.

	liveSub stan.Subscription // The subcription for the live metadata updates.
	liveCh  chan *stan.Msg

	quitCh chan struct{} // Channel to sign a stop for a particular vizier
	once   sync.Once
}

func (vz *VizierState) stop() {
	vz.once.Do(func() {
		close(vz.quitCh)
		if vz.liveSub != nil {
			vz.liveSub.Unsubscribe()
			vz.liveSub = nil
		}
	})
}

type concurrentViziersMap struct {
	unsafeMap map[uuid.UUID]*VizierState
	mapMu     sync.RWMutex
}

func (c *concurrentViziersMap) read(vizierID uuid.UUID) *VizierState {
	c.mapMu.RLock()
	defer c.mapMu.RUnlock()
	return c.unsafeMap[vizierID]
}

func (c *concurrentViziersMap) write(vizierID uuid.UUID, vz *VizierState) {
	c.mapMu.Lock()
	defer c.mapMu.Unlock()
	c.unsafeMap[vizierID] = vz
}

func (c *concurrentViziersMap) delete(vizierID uuid.UUID) {
	c.mapMu.Lock()
	defer c.mapMu.Unlock()
	delete(c.unsafeMap, vizierID)
}

// MetadataReader reads updates from the NATS durable queue and sends updates to the indexer.
type MetadataReader struct {
	db *sqlx.DB

	sc stan.Conn
	nc *nats.Conn

	viziers *concurrentViziersMap // Map of Vizier ID to it's state.

	quitCh chan struct{} // Channel to signal a stop for all viziers
	once   sync.Once
}

// NewMetadataReader creates a new MetadataReader.
func NewMetadataReader(db *sqlx.DB, sc stan.Conn, nc *nats.Conn) (*MetadataReader, error) {
	viziers := &concurrentViziersMap{unsafeMap: make(map[uuid.UUID]*VizierState)}

	m := &MetadataReader{db: db, sc: sc, nc: nc, viziers: viziers, quitCh: make(chan struct{})}
	err := m.loadState()
	if err != nil {
		m.Stop()
		return nil, err
	}

	return m, nil
}

// listenForViziers listens for any newly connected Viziers and subscribes to their update channel.
func (m *MetadataReader) listenForViziers() {
	ch := make(chan *nats.Msg)
	sub, err := m.nc.ChanSubscribe(messages.VizierConnectedChannel, ch)
	if err != nil {
		log.WithError(err).Error("Failed to listen for connected viziers")
		return
	}

	defer sub.Unsubscribe()
	defer close(ch)
	for {
		select {
		case <-m.quitCh:
			log.Info("Quit signaled")
			return
		case msg := <-ch:
			vcMsg := &messagespb.VizierConnected{}
			err := proto.Unmarshal(msg.Data, vcMsg)
			if err != nil {
				log.WithError(err).Error("Could not unmarshal VizierConnected msg")
			}
			vzID := utils.UUIDFromProtoOrNil(vcMsg.VizierID)
			log.WithField("VizierID", vzID.String()).Info("Listening to metadata updates for Vizier")

			err = m.startVizierUpdates(vzID, vcMsg.K8sUID)
			if err != nil {
				log.WithError(err).WithField("VizierID", vzID.String()).Error("Could not start listening to updates from Vizier")
			}
		}
	}
}

func (m *MetadataReader) loadState() error {
	// Start listening for any newly connected Viziers.
	go m.listenForViziers()

	// Start listening to updates for any Viziers that are already connected to Cloud.
	err := m.listenToConnectedViziers()
	if err != nil {
		log.WithError(err).Info("Failed to load state")
		return err
	}

	return nil
}

func (m *MetadataReader) listenToConnectedViziers() error {
	query := `SELECT vizier_cluster.id, vizier_cluster.cluster_uid
	    FROM vizier_cluster, vizier_cluster_info
	    WHERE vizier_cluster_info.vizier_cluster_id=vizier_cluster.id
				AND vizier_cluster_info.status != 'DISCONNECTED'
				AND vizier_cluster_info.status != 'UNKNOWN'`
	var val struct {
		VizierID uuid.UUID `db:"id"`
		K8sUID   string    `db:"cluster_uid"`
	}

	rows, err := m.db.Queryx(query)
	if err != nil {
		return err
	}
	defer rows.Close()
	for rows.Next() {
		err := rows.StructScan(&val)
		if err != nil {
			return err
		}
		err = m.startVizierUpdates(val.VizierID, val.K8sUID)
		if err != nil {
			return err
		}
	}

	return nil
}

// startVizierUpdates starts listening to the metadata update channel for a given vizier.
func (m *MetadataReader) startVizierUpdates(id uuid.UUID, k8sUID string) error {
	// TODO(michelle): We currently don't have to signal when a Vizier has disconnected. When we have that
	// functionality, we should clean up the Vizier map and stop its STAN subscriptions.
	vz := m.viziers.read(id)
	if vz != nil {
		log.WithField("vizier_id", id.String()).Info("Already listening to metadata updates from Vizier")
		vz.k8sUID = k8sUID // Update the K8s uid, in case it has changed with the newly connected Vizier.
		return nil
	}

	vzState := &VizierState{
		id:     id,
		k8sUID: k8sUID,
		liveCh: make(chan *stan.Msg),
		quitCh: make(chan struct{}),
	}

	// Subscribe to STAN topic for streaming updates.
	topic := vzshard.V2CTopic(streamingMetadataTopic, id)
	log.WithField("topic", topic).Info("Subscribing to STAN")
	liveSub, err := m.sc.Subscribe(topic, func(msg *stan.Msg) {
		vzState.liveCh <- msg
	}, stan.SetManualAckMode())
	if err != nil {
		vzState.stop()
		return err
	}
	vzState.liveSub = liveSub

	m.viziers.write(id, vzState)
	go m.processVizierUpdates(vzState)
	return nil
}

// stopVizierUpdates stops listening to the metadata update channel for a given vizier.
func (m *MetadataReader) stopVizierUpdates(id uuid.UUID) error {
	vz := m.viziers.read(id)
	if vz != nil {
		vz.stop()
		m.viziers.delete(id)
		return nil
	}

	return errors.New("Vizier doesn't exist")
}

// processVizierUpdates reads from the metadata updates and sends them to the indexer in order.
func (m *MetadataReader) processVizierUpdates(vzState *VizierState) error {
	// Clean up if any error has occurred.
	defer m.stopVizierUpdates(vzState.id)

	for {
		select {
		case <-m.quitCh:
			return nil
		case <-vzState.quitCh:
			return nil
		case msg := <-vzState.liveCh:
			if msg == nil {
				continue
			}

			update, err := readMetadataUpdate(msg.Data)
			if err != nil {
				return err
			}
			err = m.processVizierUpdate(update, vzState)
			if err != nil {
				log.WithError(err).Error("Error processing Vizier metadata updates")
				return err
			}
			msg.Ack()
		}
	}
}

// Stop shuts down the metadata reader and all relevant goroutines.
func (m *MetadataReader) Stop() {
	m.once.Do(func() {
		close(m.quitCh)
	})
}

func readMetadataUpdate(data []byte) (*metadatapb.ResourceUpdate, error) {
	v2cMsg := &cvmsgspb.V2CMessage{}
	err := proto.Unmarshal(data, v2cMsg)
	if err != nil {
		return nil, err
	}
	updateMsg := &metadatapb.ResourceUpdate{}
	err = types.UnmarshalAny(v2cMsg.Msg, updateMsg)
	if err != nil {
		log.WithError(err).Error("Could not unmarshal metadata update message")
		return nil, err
	}
	return updateMsg, nil
}

func readMetadataResponse(data []byte) (*metadatapb.MissingK8SMetadataResponse, error) {
	v2cMsg := &cvmsgspb.V2CMessage{}
	err := proto.Unmarshal(data, v2cMsg)
	if err != nil {
		return nil, err
	}
	updates := &metadatapb.MissingK8SMetadataResponse{}
	err = types.UnmarshalAny(v2cMsg.Msg, updates)
	if err != nil {
		log.WithError(err).Error("Could not unmarshal metadata response message")
		return nil, err
	}
	return updates, nil
}

func wrapMetadataRequest(vizierID uuid.UUID, req *metadatapb.MissingK8SMetadataRequest) ([]byte, error) {
	reqAnyMsg, err := types.MarshalAny(req)
	if err != nil {
		return nil, err
	}
	c2vMsg := cvmsgspb.C2VMessage{
		VizierID: vizierID.String(),
		Msg:      reqAnyMsg,
	}
	b, err := c2vMsg.Marshal()
	if err != nil {
		return nil, err
	}
	return b, nil
}

func (m *MetadataReader) processVizierUpdate(update *metadatapb.ResourceUpdate, vzState *VizierState) error {
	if update.UpdateVersion < vzState.updateVersion {
		// Old update, drop.
		return nil
	}

	if update.PrevUpdateVersion != vzState.updateVersion {
		err := m.getMissingUpdates(vzState.updateVersion, update.UpdateVersion, vzState)
		if err != nil {
			return err
		}
	}
	return m.applyMetadataUpdate(vzState, update)
}

func (m *MetadataReader) getMissingUpdates(from, to int64, vzState *VizierState) error {
	log.
		WithField("vizier", vzState.id.String()).
		WithField("from", from).
		WithField("to", to).
		Info("Making request for missing metadata updates")

	mdReq := &metadatapb.MissingK8SMetadataRequest{
		FromUpdateVersion: from,
		ToUpdateVersion:   to,
	}
	reqBytes, err := wrapMetadataRequest(vzState.id, mdReq)
	if err != nil {
		return err
	}

	// Subscribe to topic that the response will be sent on.
	subCh := make(chan *nats.Msg, 1024)
	sub, err := m.nc.ChanSubscribe(vzshard.V2CTopic(metadataResponseTopic, vzState.id), subCh)
	defer sub.Unsubscribe()

	pubTopic := vzshard.C2VTopic(metadataRequestTopic, vzState.id)
	err = m.nc.Publish(pubTopic, reqBytes)
	if err != nil {
		return err
	}

	for {
		select {
		case <-m.quitCh:
			return errors.New("Quit signaled")
		case <-vzState.quitCh:
			return errors.New("Vizier removed")
		case msg := <-subCh:
			updatesResponse, err := readMetadataResponse(msg.Data)
			if err != nil {
				return err
			}

			updates := updatesResponse.Updates
			if len(updates) == 0 {
				if vzState.updateVersion < updatesResponse.FirstUpdateAvailable-1 {
					vzState.updateVersion = updatesResponse.FirstUpdateAvailable - 1
				}
				return nil
			}

			firstUpdate := updates[0]
			if firstUpdate.UpdateVersion == updatesResponse.FirstUpdateAvailable {
				if vzState.updateVersion < firstUpdate.PrevUpdateVersion {
					vzState.updateVersion = firstUpdate.PrevUpdateVersion
				}
			}

			for _, update := range updates {
				err := m.processVizierUpdate(update, vzState)
				if err != nil {
					return err
				}
			}

			// Check to see if this batch contains the last of the missing updates we expect.
			lastUpdate := updates[len(updates)-1]
			if lastUpdate.UpdateVersion == to {
				return nil
			}
			if lastUpdate.UpdateVersion == updatesResponse.LastUpdateAvailable {
				return nil
			}
		case <-time.After(20 * time.Minute):
			// Our previous request shouldn't have gotten lost on NATS, so if there is a subscriber for the metadata
			// requests we shouldn't actually need to resend the request.
			return nil
		}
	}
}

func (m *MetadataReader) applyMetadataUpdate(vzState *VizierState, update *metadatapb.ResourceUpdate) error {
	// Publish update to the indexer.
	b, err := update.Marshal()
	if err != nil {
		return err
	}

	log.
		WithField("topic", fmt.Sprintf("%s.%s", indexerMetadataTopic, vzState.k8sUID)).
		WithField("rv", update.UpdateVersion).
		Info("Publishing metadata update to indexer")

	err = m.sc.Publish(fmt.Sprintf("%s.%s", indexerMetadataTopic, vzState.k8sUID), b)
	if err != nil {
		return err
	}

	vzState.updateVersion = update.UpdateVersion
	return nil
}
