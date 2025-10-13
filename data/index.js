// 原理说明：前端脚本定期轮询状态与消息接口，更新界面并将用户操作打包成 JSON 命令发送至 ESP。
const el = {
  wifi: document.getElementById('wifi-status'),
  espIp: document.getElementById('ip-address'),
  stm32Ip: document.getElementById('stm32-ip'),
  uptime: document.getElementById('uptime'),
  sensorHint: document.getElementById('sensor-hint'),
  temp: document.getElementById('temp-value'),
  humi: document.getElementById('humi-value'),
  soil: document.getElementById('soil-value'),
  lux: document.getElementById('lux-value'),
  water: document.getElementById('water-value'),
  light: document.getElementById('light-value'),
  fan: document.getElementById('fan-value'),
  dataAge: document.getElementById('data-age'),
  ackCard: document.getElementById('ack-card'),
  messageLog: document.getElementById('message-log'),
  commandForm: document.getElementById('command-form'),
  commandTarget: document.getElementById('command-target'),
  commandAction: document.getElementById('command-action'),
  commandTime: document.getElementById('command-time'),
  timeWrapper: document.getElementById('time-wrapper'),
  commandHint: document.getElementById('command-hint'),
  globalError: document.getElementById('global-error'),
};

let lastMessageId = 0;
const messageBuffer = [];
const maxBufferSize = 120;

function formatDuration(seconds) {
  if (seconds < 60) {
    return `${seconds} 秒`;
  }
  const minutes = Math.floor(seconds / 60);
  const remainingSeconds = seconds % 60;
  if (minutes < 60) {
    return `${minutes} 分 ${remainingSeconds} 秒`;
  }
  const hours = Math.floor(minutes / 60);
  const remainingMinutes = minutes % 60;
  if (hours < 24) {
    return `${hours} 小时 ${remainingMinutes} 分`;
  }
  const days = Math.floor(hours / 24);
  const remainingHours = hours % 24;
  return `${days} 天 ${remainingHours} 小时`;
}

function formatAge(ageMs) {
  if (Number.isNaN(ageMs)) {
    return '--';
  }
  if (ageMs < 1000) {
    return `${ageMs} ms`;
  }
  const seconds = Math.floor(ageMs / 1000);
  return `${seconds} 秒前`;
}

function appendMessageToLog(jsonLine) {
  if (!jsonLine) {
    return;
  }
  messageBuffer.push(jsonLine);
  if (messageBuffer.length > maxBufferSize) {
    messageBuffer.splice(0, messageBuffer.length - maxBufferSize);
  }
  el.messageLog.textContent = messageBuffer.join('\n');
}

function showError(message) {
  el.globalError.textContent = message;
  el.globalError.hidden = false;
  setTimeout(() => {
    el.globalError.hidden = true;
  }, 5000);
}

function updateStatusView(state) {
  const wifi = state.wifi ?? {};
  el.wifi.textContent = wifi.connected ? '已连接' : '未连接';
  el.espIp.textContent = wifi.ip || '未知';
  el.stm32Ip.textContent = state.stm32ReportedIp || '未上报';
  el.uptime.textContent = formatDuration(state.uptimeSeconds ?? 0);
}

function updateSensorView(state) {
  if (!state.latestData) {
    el.sensorHint.textContent = '等待 STM32 上传数据...';
    el.sensorHint.hidden = false;
    return;
  }

  const data = state.latestData;
  el.sensorHint.hidden = true;
  el.temp.textContent = `${data.temp?.toFixed?.(1) ?? data.temp ?? '--'} ℃`;
  el.humi.textContent = `${data.humi?.toFixed?.(1) ?? data.humi ?? '--'} %`;
  el.soil.textContent = `${data.soil ?? '--'} %`;
  el.lux.textContent = `${data.lux?.toFixed?.(1) ?? data.lux ?? '--'} lx`;
  el.water.textContent = data.water ? '开启' : '关闭';
  el.light.textContent = data.light ? '开启' : '关闭';
  el.fan.textContent = data.fan ? '开启' : '关闭';
  el.dataAge.textContent = formatAge(data.ageMs);
}

function updateAckView(state) {
  if (!state.latestAck) {
    el.ackCard.classList.remove('ok');
    el.ackCard.textContent = '尚未收到回执。';
    return;
  }

  const ack = state.latestAck;
  el.ackCard.classList.toggle('ok', ack.result === 'ok');
  el.ackCard.innerHTML = `
    <strong>目标：</strong>${ack.target}<br>
    <strong>动作：</strong>${ack.action}<br>
    <strong>结果：</strong>${ack.result}<br>
    <strong>延迟：</strong>${formatAge(ack.ageMs)}
  `;
}

async function fetchState() {
  try {
    const response = await fetch('/api/state');
    if (!response.ok) {
      throw new Error(`STATE ${response.status}`);
    }
    const state = await response.json();
    updateStatusView(state);
    updateSensorView(state);
    updateAckView(state);
  } catch (error) {
    showError(`状态刷新失败：${error.message}`);
  }
}

async function fetchMessages() {
  try {
    const response = await fetch(`/api/messages?after=${lastMessageId}`);
    if (!response.ok) {
      throw new Error(`MESSAGES ${response.status}`);
    }

    const lastIdHeader = response.headers.get('X-Last-Message-Id');
    if (lastIdHeader) {
      const parsed = Number(lastIdHeader);
      if (!Number.isNaN(parsed) && parsed > lastMessageId) {
        lastMessageId = parsed;
      }
    }

    const text = await response.text();
    if (!text.trim()) {
      return;
    }

    text
      .split('\n')
      .map((line) => line.trim())
      .filter((line) => line.length > 0)
      .forEach((line) => {
        try {
          const json = JSON.parse(line);
          appendMessageToLog(JSON.stringify(json, null, 2));
        } catch (error) {
          appendMessageToLog(line);
        }
      });
  } catch (error) {
    showError(`消息刷新失败：${error.message}`);
  }
}

async function sendCommand(payload) {
  const response = await fetch('/api/cmd', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const msg = await response.text();
    throw new Error(msg || `HTTP ${response.status}`);
  }
  const data = await response.json();
  el.commandHint.textContent = `命令已发送，消息序号 ${data.queuedId ?? '未知'}`;
}

function handleCommandSubmit(event) {
  event.preventDefault();
  const payload = {
    target: el.commandTarget.value,
    action: el.commandAction.value,
  };
  if (payload.action === 'pulse') {
    payload.time = Number(el.commandTime.value);
  }

  sendCommand(payload).catch((error) => {
    showError(`命令发送失败：${error.message}`);
  });
}

function handleActionChange() {
  const isPulse = el.commandAction.value === 'pulse';
  el.timeWrapper.hidden = !isPulse;
}

function bootstrap() {
  el.commandForm.addEventListener('submit', handleCommandSubmit);
  el.commandAction.addEventListener('change', handleActionChange);
  handleActionChange();

  fetchState();
  fetchMessages();
  setInterval(fetchState, 5000);
  setInterval(fetchMessages, 2000);
}

bootstrap();
