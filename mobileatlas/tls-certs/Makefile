cn_suffix = localhost

.PHONY: clean all
.NOTINTERMEDIATE: private/%.key

all: certs/server.crt certs/client.crt

clean:
	rm -rf certs/ csr/ private/ index.txt{,.old} index.txt.attr{,.old} serial{,.old}

private/%.key:
	mkdir -p private
	openssl ecparam -genkey -name prime256v1 -noout -out "private/$*.key"

csr/%.csr: private/%.key
	mkdir -p csr
	openssl req -config openssl.cnf -key "$<" -new -out "$@" -subj "/CN=$*.$(cn_suffix)" -addext "subjectAltName = DNS:$*.$(cn_suffix)"

certs/ca.crt: private/ca.key
	mkdir -p certs
	openssl req -config openssl.cnf -key private/ca.key -new -x509 -out "$@" -subj "/CN=ca.$(cn_suffix)"

serial:
	echo 1000 > "$@"

certs/server.crt: csr/server.csr certs/ca.crt private/ca.key serial
	mkdir -p certs
	touch index.txt
	openssl ca -quiet -config openssl.cnf -extensions server_cert -notext -in "$<"
	mv -i certs/$$(cut -f4 index.txt | tail -1).pem certs/server.crt

certs/client.crt: csr/client.csr certs/ca.crt private/ca.key serial
	mkdir -p certs
	touch index.txt
	openssl ca -quiet -config openssl.cnf -extensions usr_cert -notext -in "$<"
	mv -i certs/$$(cut -f4 index.txt | tail -1).pem certs/client.crt
