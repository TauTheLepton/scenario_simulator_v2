docker pull pseudomuto/protoc-gen-doc
docker run --rm \
  -v $(pwd)/../simulation/xmlrpc_interface/proto:/protos \
  -v $(pwd)/proto_doc:/out \
  pseudomuto/protoc-gen-doc