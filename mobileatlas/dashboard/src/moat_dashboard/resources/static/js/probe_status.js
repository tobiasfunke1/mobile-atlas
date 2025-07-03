const probe_id = document.getElementById("probe-id").textContent;

async function get_hist() {
  const res = await fetch(`/probes/${probe_id}/status/hist`);
  const json = await res.json();

  console.debug("Probe status hist response: %o", json);

  return json;
}

function human(s) {
  if (s > 10 ** 9) {
    return `${(s / 10 ** 9).toFixed(2)}\u202FGB`;
  }
  if (s > 10 ** 6) {
    return `${(s / 10 ** 6).toFixed(2)}\u202FMB`;
  }
  if (s > 10 ** 3) {
    return `${(s / 10 ** 3).toFixed(2)}\u202FKB`;
  }

  return `${s}\u202FB`;
}

function update_details(data) {
  const d = data.at(-1);
  document.querySelector("#temperature > dd").innerText =
    `${d.temperature.toFixed(1)}\u202F°C`;
  document.querySelector("#uptime > dd").innerText = luxon.Duration.fromISO(
    d.uptime,
  ).toHuman({ unitDisplay: "short" });
  document.querySelector("#timestamp > dd").innerText = luxon.DateTime.fromISO(
    d.timestamp,
  ).toLocaleString(luxon.DateTime.DATETIME_SHORT);
  document.querySelector("#rxtx > dd").innerText =
    `${human(d.rx_bytes)} / ${human(d.tx_bytes)}`;
}

async function setup_charts() {
  const data = await get_hist();
  const ts = data.map((s) => s.timestamp);
  const temp_data = data.map((s) => s.temperature);
  const rx_data = data.map((s) => s.rx_bytes);
  const tx_data = data.map((s) => s.tx_bytes);

  const temp_chart = new Chart(document.getElementById("temp-chart"), {
    type: "line",
    data: {
      labels: ts,
      datasets: [
        {
          label: "Temperature",
          data: temp_data,
        },
      ],
    },
    options: {
      scales: {
        x: {
          type: "time",
          time: {
            unit: "minute",
          },
        },
        y: {
          title: {
            text: "°C",
            display: true,
          },
        },
      },
    },
  });

  const rxtx_chart = new Chart(document.getElementById("rxtx-chart"), {
    type: "line",
    data: {
      labels: ts,
      datasets: [
        {
          label: "Received",
          data: rx_data,
        },
        {
          label: "Transmitted",
          data: tx_data,
        },
      ],
    },
    options: {
      scales: {
        x: {
          type: "time",
          time: {
            unit: "minute",
          },
        },
        y: {
          title: {
            text: "Bytes",
            display: true,
          },
        },
      },
    },
  });

  const ws = new WebSocket(`/probes/${probe_id}/status/ws`);
  ws.addEventListener("message", (e) => {
    const data = JSON.parse(e.data);
    console.debug("Received status update: %o", data);

    update_details(data);

    for (const s of data) {
      ts.push(s.timestamp);
      temp_data.push(s.temperature);
      rx_data.push(s.rx_bytes);
      tx_data.push(s.tx_bytes);
    }

    const now = luxon.DateTime.now();
    const idx = ts.findIndex(
      (ts) =>
        now.diff(luxon.DateTime.fromISO(ts)) <
        luxon.Duration.fromObject({ hours: 24 }),
    );
    ts.splice(0, idx);
    temp_data.splice(0, idx);
    rx_data.splice(0, idx);
    tx_data.splice(0, idx);

    temp_chart.update();
    rxtx_chart.update();
  });
}

setup_charts();

for (const e of document.querySelectorAll("time[datetime]")) {
  const dt = luxon.DateTime.fromISO(e.attributes.datetime.value);
  if (dt.isValid) {
    e.innerText = dt.toLocaleString(luxon.DateTime.DATETIME_SHORT);
  }
}
