# HTTP server

To run, first build everything:
```
bazel run //src/stirling/testing/demo_apps/go_https/server:golang_1_16_https_server
bazel run //src/stirling/testing/demo_apps/go_https/client:golang_1_16_https_client
```

Then execute the following commands in two separate terminals:

```
docker run --name=go_https_server bazel/src/stirling/testing/demo_apps/go_https/server:golang_1_16_https_server
```

```
docker run --name=go_https_client --network=container:go_https_server bazel/src/stirling/testing/demo_apps/go_https/client:golang_1_16_https_client --iters 3 --sub_iters 3
```
