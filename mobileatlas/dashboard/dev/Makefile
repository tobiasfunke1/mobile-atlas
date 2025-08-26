VENV_DIR:=venv

dev: venv env
	source "${VENV_DIR}/bin/activate" && \
	source ./env && \
	moat-dashboard


venv:
	python3 -m venv "${VENV_DIR}" && \
	source "${VENV_DIR}/bin/activate" && \
	pip install -r ../requirements.txt && \
	pip install -e ..

compose:
	podman compose up -d

env: compose
	./setup.sh

.PHONY: dev compose

clean:
	podman compose down
	rm -rf "${VENV_DIR}" env
