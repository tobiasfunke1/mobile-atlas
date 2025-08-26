FROM python:3

WORKDIR /usr/src/app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY pyproject.toml MANIFEST.in ./
COPY src ./src
RUN pip install --no-cache-dir .

CMD ["moat-dash-migrate"]
