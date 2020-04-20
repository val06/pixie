package live

import (
	"context"
	"fmt"
	"strconv"
	"time"
	"unicode"

	"github.com/gdamore/tcell"
	"github.com/rivo/tview"
	"pixielabs.ai/pixielabs/src/utils/pixie_cli/pkg/components"
	"pixielabs.ai/pixielabs/src/utils/pixie_cli/pkg/script"
	"pixielabs.ai/pixielabs/src/utils/pixie_cli/pkg/vizier"
)

const (
	debugShowBorders = false
	maxCellSize      = 50
	logoColor        = "#3FE7E7"
	textColor        = "#ffffff"
	accentColor      = "#008B8B"
	bgColor          = "#000000"
)

// appState is the global state that is used by the live view.
type appState struct {
	br *script.BundleReader
	ac autocompleter

	vizier *vizier.Connector
	// The last script that was executed. If nil, nothing was executed.
	execScript *script.ExecutableScript
	// The view of all the tables in the current execution.
	tables []components.TableView

	// ----- View Specific State ------
	// tview does not allow us to access page names so we hang onto the pages we create here.
	pageNames []string
	// The currently selected table. Will reset to zero when new tables are inserted.
	selectedTable int
}

// View is the top level of the Live View.
type View struct {
	app           *tview.Application
	pages         *tview.Pages
	tableSelector *tview.TextView
	infoView      *tview.TextView
	modal         Modal
	s             *appState
}

// Modal is the interface for a pop-up view.
type Modal interface {
	Show(a *tview.Application) tview.Primitive
	Close(a *tview.Application)
}

// New creates a new live view.
func New(br *script.BundleReader, vizier *vizier.Connector, execScript *script.ExecutableScript) (*View, error) {
	// App is the top level view. The layout is approximately as follows:
	//  ------------------------------------------
	//  | View Information ...                   |
	//  |________________________________________|
	//  | The actual tables                      |
	//  |                                        |
	//  |                                        |
	//  |                                        |
	//  |________________________________________|
	//  | Table Selector                | Logo   |
	//  ------------------------------------------

	// Top of page.
	infoView := tview.NewTextView()
	infoView.
		SetScrollable(false).
		SetDynamicColors(true).
		SetBorder(debugShowBorders)
	infoView.SetBorderPadding(1, 0, 0, 0)
	topBar := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(infoView, 3, 0, true)

	// Middle of page.
	pages := tview.NewPages()
	pages.SetBorder(debugShowBorders)

	// Bottom of Page.
	logoBox := tview.NewTextView().
		SetScrollable(false).
		SetDynamicColors(true)

	// Print out the logo.
	fmt.Fprintf(logoBox, "\n  [%s]PIXIE[%s]", logoColor, textColor)

	tableSelector := tview.NewTextView()
	bottomBar := tview.NewFlex().
		SetDirection(tview.FlexColumn).
		AddItem(tableSelector, 0, 1, false).
		AddItem(logoBox, 8, 1, false)
	bottomBar.SetBorderPadding(1, 0, 0, 0)

	layout := tview.NewFlex().
		SetDirection(tview.FlexRow).
		AddItem(topBar, 3, 0, false).
		AddItem(pages, 0, 1, true).
		AddItem(bottomBar, 2, 0, false)

	// Application setup.
	app := tview.NewApplication()
	app.SetRoot(layout, true).
		EnableMouse(true)

	v := &View{
		app:           app,
		pages:         pages,
		tableSelector: tableSelector,
		infoView:      infoView,
		s: &appState{
			br:     br,
			vizier: vizier,
			ac:     newFuzzyAutoCompleter(br),
		},
	}

	// Wire up components.
	tableSelector.
		SetDynamicColors(true).
		SetRegions(true).
		SetWrap(false)

	// When table selector is highlighted (ie. mouse click or number). We use the region
	// to select the appropriate table.
	tableSelector.SetHighlightedFunc(func(added, removed, remaining []string) {
		if len(added) > 0 {
			if tableNum, err := strconv.Atoi(added[0]); err == nil {
				v.selectTable(tableNum)
			}
		}
	})

	// If a default script was passed in execute it.
	err := v.runScript(execScript)
	if err != nil {
		return nil, err
	}

	// Wire up the main keyboard handler.
	app.SetInputCapture(v.keyHandler)
	return v, nil
}

// Run runs the view.
func (v *View) Run() error {
	return v.app.Run()
}

// Stop stops the view and kills the app.
func (v *View) Stop() {
	v.app.Stop()
}

// runScript is the internal method to run an executable script and update relevant appState.
func (v *View) runScript(execScript *script.ExecutableScript) error {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	resp, err := vizier.RunScript(ctx, v.s.vizier, execScript)
	if err != nil {
		return err
	}
	tw := vizier.NewVizierStreamOutputAdapter(ctx, resp, vizier.FormatInMemory)
	tw.Finish()

	v.s.tables = tw.Views()
	v.s.execScript = execScript
	v.s.selectedTable = 0

	v.execCompleteViewUpdate()
	return nil
}

func (v *View) execCompleteViewUpdate() {
	v.closeModal()

	v.updateScriptInfoView()
	v.updateTableNav()
	v.updateTableView()
}

func (v *View) updateScriptInfoView() {
	v.infoView.Clear()

	fmt.Fprintf(v.infoView, "%s %s\n", withAccent("Script Name:"),
		v.s.execScript.Metadata().ScriptName)

	if v.s.execScript.Metadata().HasVis {
		liveLink := v.s.execScript.Metadata().LiveViewLink()
		fmt.Fprintf(v.infoView, "%s %s", withAccent("Live View:"), liveLink)
	}
}

func (v *View) updateTableView() {
	// We remove all the old pages and create new pages for tables.
	for _, pageName := range v.s.pageNames {
		v.pages.RemovePage(pageName)
	}

	for idx, table := range v.s.tables {
		pageName := fmt.Sprintf(pageName(idx))
		// Iterate through each table and create a page for it.
		table := v.createTviewTable(table)
		v.s.pageNames = append(v.s.pageNames, pageName)
		v.pages.AddPage(pageName, table, true, false)
	}

	// We select the first table and set the app level focus on the main view.
	v.selectTableAndHighlight(0)
	v.app.SetFocus(v.pages)
}

func (v *View) updateTableNav() {
	v.tableSelector.Clear()
	for idx, t := range v.s.tables {
		fmt.Fprintf(v.tableSelector, `%d ["%d"]%s[""]  `, idx+1, idx, withAccent(t.Name()))
	}
}

func (v *View) selectNextTable() {
	v.selectTableAndHighlight(v.s.selectedTable + 1)
}

func (v *View) selectPrevTable() {
	v.selectTableAndHighlight(v.s.selectedTable - 1)
}

func (v *View) createTviewTable(t components.TableView) *tview.Table {
	table := tview.NewTable().
		SetBorders(true).
		SetSelectable(true, true).
		SetFixed(1, 0)

	for idx, val := range t.Header() {
		// Render the header.
		tableCell := tview.NewTableCell(withAccent(val)).
			SetAlign(tview.AlignCenter).
			SetSelectable(false).
			SetExpansion(2)
		table.SetCell(0, idx, tableCell)
	}

	for rowIdx, row := range t.Data() {
		for colIdx, val := range stringifyRow(row) {
			if len(val) > maxCellSize {
				val = val[:maxCellSize-1] + "\u2026"
			}

			tableCell := tview.NewTableCell(tview.TranslateANSI(val)).
				SetTextColor(tcell.ColorWhite).
				SetAlign(tview.AlignLeft).
				SetSelectable(true).
				SetExpansion(2)
			table.SetCell(rowIdx+1, colIdx, tableCell)
		}
	}

	handleLargeBlobView := func(row, column int) {
		v.closeModal()

		if row < 1 || column < 0 {
			return
		}

		// Try to parse large blob as a string, we only know how to render large strings
		// so bail if we can't convert to string or if it's not that big.
		d := t.Data()[row-1][column]
		s, ok := d.(string)
		if !ok || len(s) < maxCellSize {
			return
		}

		renderString := tryJSONHighlight(s)
		v.showDataModal(tview.TranslateANSI(renderString))
	}

	// Since selection and mouse clicks happen in two different events, we need to track the selection
	// rows/cols in variables so that we can show the right popup.
	selectedRow := 0
	selectedCol := 0
	table.SetMouseCapture(func(action tview.MouseAction, event *tcell.EventMouse) (tview.MouseAction, *tcell.EventMouse) {
		if action == tview.MouseLeftDoubleClick {
			handleLargeBlobView(selectedRow, selectedCol)
			// For some reason the double click event does not trigger a redraw.
			v.app.ForceDraw()
			return action, event
		}
		return action, event
	})

	table.SetSelectionChangedFunc(func(row, column int) {
		// Store the selection so we can pop open the blob view on double click.
		selectedRow = row
		selectedCol = column
		// This function is triggered when mouse is used after modal is open, in which case we can switch the blob.
		if v.modal != nil {
			handleLargeBlobView(row, column)
		}
	})

	table.SetSelectedFunc(handleLargeBlobView)

	return table
}

func (v *View) showDataModal(s string) {
	v.closeModal()
	d := newDetailsModal(s)
	m := d.Show(v.app)
	v.pages.AddPage("modal", createModal(m, 60, 30), true, true)
	v.modal = d
}

func (v *View) showAutcompleteModal() {
	v.closeModal()
	ac := newAutocompleteModal(v.s)
	ac.setScriptExecFunc(func(s *script.ExecutableScript) {
		v.runScript(s)
	})
	v.modal = ac
	v.pages.AddPage("modal", createModal(v.modal.Show(v.app),
		65, 30), true, true)
}

// closes modal if open, noop if not.
func (v *View) closeModal() {
	if v.modal == nil {
		return
	}
	v.pages.RemovePage("modal")
	v.modal = nil
	// This will cause a refocus to occur on the table.
	v.selectTableAndHighlight(v.s.selectedTable)
}

// selectTableAndHighlight selects and highligts the table. Don't call this from within the highlight func
// or you will get an infinite loop.
func (v *View) selectTableAndHighlight(tableNum int) {
	tableNum = v.selectTable(tableNum)
	v.tableSelector.Highlight(strconv.Itoa(tableNum)).ScrollToHighlight()
}

// selectTable selects the numbered table. Out of bounds wrap in both directions.
func (v *View) selectTable(tableNum int) int {
	if len(v.s.tables) == 0 {
		return 0
	}
	tableNum = tableNum % len(v.s.tables)

	v.pages.SwitchToPage(pageName(tableNum))
	v.s.selectedTable = tableNum
	v.app.SetFocus(v.pages)

	return tableNum
}

func (v *View) keyHandler(event *tcell.EventKey) *tcell.EventKey {
	// If the modal is open capture the event and only let
	// escape work to close the modal.
	if v.modal != nil {
		if event.Key() == tcell.KeyEscape {
			v.closeModal()
		}
		return event
	}

	switch event.Key() {
	case tcell.KeyTAB:
		// Default for tab is to quit so stop that.
		return nil
	case tcell.KeyCtrlN:
		v.selectNextTable()
	case tcell.KeyCtrlP:
		v.selectPrevTable()
	case tcell.KeyRune:
		// Switch to a specific view. This will be a no-op if no tables are loaded.
		r := event.Rune()
		if unicode.IsDigit(r) {
			v.selectTableAndHighlight(int(r-'0') - 1)
		}
	case tcell.KeyCtrlK:
		v.showAutcompleteModal()
		return nil
	}

	// Ctrl-c, etc. can happen based on default handlers.
	return event
}

// TODO(zasgar): Share this functions with regular table renderer.
type stringer interface {
	String() string
}

func stringifyRow(row []interface{}) []string {
	s := make([]string, len(row))

	for i, val := range row {
		switch u := val.(type) {
		case time.Time:
			s[i] = u.Format(time.RFC3339)
		case stringer:
			s[i] = u.String()
		case float64:
			s[i] = fmt.Sprintf("%0.2f", u)
		default:
			s[i] = fmt.Sprintf("%+v", u)
		}
	}
	return s
}
