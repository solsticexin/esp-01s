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
  buzzer: document.getElementById('buzzer-value'),
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
  thresholdForm: document.getElementById('threshold-form'),
  thresholdTemp: document.getElementById('threshold-temp'),
  thresholdHumi: document.getElementById('threshold-humi'),
  thresholdSoil: document.getElementById('threshold-soil'),
  thresholdLux: document.getElementById('threshold-lux'),
  thresholdHint: document.getElementById('threshold-hint'),
  alarmStatus: document.getElementById('alarm-status'),
};

let lastMessageId = 0;
const messageBuffer = [];
const maxBufferSize = 120;
const thresholdInputs = [el.thresholdTemp, el.thresholdHumi, el.thresholdSoil, el.thresholdLux].filter(Boolean);
let thresholdFormDirty = false;
const thresholdDefaultHint = '留空表示禁用，超限时将触发蜂鸣报警。';

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
  if (typeof ageMs !== 'number' || Number.isNaN(ageMs)) {
    return '--';
  }
  if (ageMs < 1000) {
    return `${ageMs} ms`;
  }
  const seconds = Math.floor(ageMs / 1000);
  return `${seconds} 秒前`;
}

function formatSwitchState(value) {
  if (value === 1 || value === true) {
    return '开启';
  }
  if (value === 0 || value === false) {
    return '关闭';
  }
  if (typeof value === 'number') {
    return value > 0 ? '开启' : '关闭';
  }
  return '--';
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
  el.water.textContent = formatSwitchState(data.water);
  el.light.textContent = formatSwitchState(data.light);
  el.fan.textContent = formatSwitchState(data.fan);
  el.buzzer.textContent = formatSwitchState(data.buzzer);
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

function resetThresholdHint() {
  if (el.thresholdHint) {
    el.thresholdHint.textContent = thresholdDefaultHint;
  }
}

function updateAlarmView(alarm) {
  if (!el.alarmStatus) {
    return;
  }
  if (!alarm) {
    el.alarmStatus.textContent = '暂无报警记录。';
    return;
  }

  const parts = [];
  parts.push(`累计触发 ${alarm.count ?? 0} 次`);

  if (alarm.reason) {
    parts.push(`最近原因：${alarm.reason}`);
  }

  if (typeof alarm.ageMs === 'number') {
    parts.push(`上次触发：${formatAge(alarm.ageMs)}`);
  }

  if (alarm.cooldownMs) {
    parts.push(`冷却：${Math.round(alarm.cooldownMs / 1000)} 秒`);
  }

  if (alarm.pulseMs) {
    parts.push(`蜂鸣：${alarm.pulseMs} ms`);
  }

  el.alarmStatus.innerHTML = parts.join('<br>');
}

function updateThresholdView(state) {
  if (!el.thresholdForm) {
    return;
  }

  const thresholds = state?.thresholds ?? {};
  const active = document.activeElement;
  const isEditing = thresholdFormDirty && thresholdInputs.includes(active);
  if (!isEditing) {
    const assign = (input, value) => {
      if (!input) {
        return;
      }
      if (value === null || value === undefined || Number.isNaN(value)) {
        input.value = '';
      } else {
        input.value = value;
      }
    };

    assign(el.thresholdTemp, thresholds.temp);
    assign(el.thresholdHumi, thresholds.humi);
    assign(el.thresholdSoil, thresholds.soil);
    assign(el.thresholdLux, thresholds.lux);
    thresholdFormDirty = false;
    resetThresholdHint();
  }

  updateAlarmView(state?.alarm);
}

function readThresholdInput(input, label) {
  if (!input) {
    return null;
  }
  const raw = input.value.trim();
  if (raw === '') {
    return null;
  }
  const numeric = Number(raw);
  if (Number.isNaN(numeric)) {
    throw new Error(`${label} 请输入数字`);
  }
  return numeric;
}

async function updateThresholdsOnServer(payload) {
  const response = await fetch('/api/thresholds', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
  if (!response.ok) {
    const msg = await response.text();
    throw new Error(msg || `HTTP ${response.status}`);
  }
  return response.json();
}

async function handleThresholdSubmit(event) {
  event.preventDefault();
  try {
    const payload = {
      temp: readThresholdInput(el.thresholdTemp, '温度阈值'),
      humi: readThresholdInput(el.thresholdHumi, '湿度阈值'),
      soil: readThresholdInput(el.thresholdSoil, '土壤阈值'),
      lux: readThresholdInput(el.thresholdLux, '光照阈值'),
    };
    const data = await updateThresholdsOnServer(payload);
    thresholdFormDirty = false;
    el.thresholdHint.textContent = '阈值已更新并已提交到设备。';
    setTimeout(resetThresholdHint, 4000);
    updateThresholdView(data);
  } catch (error) {
    showError(`阈值更新失败：${error.message}`);
  }
}

function markThresholdDirty() {
  thresholdFormDirty = true;
  el.thresholdHint.textContent = '存在未保存的阈值修改。';
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
    updateThresholdView(state);
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

  if (el.thresholdForm) {
    el.thresholdForm.addEventListener('submit', handleThresholdSubmit);
    thresholdInputs.forEach((input) => {
      if (input) {
        input.addEventListener('input', markThresholdDirty);
      }
    });
    resetThresholdHint();
  }

  fetchState();
  fetchMessages();
  setInterval(fetchState, 5000);
  setInterval(fetchMessages, 2000);
}

bootstrap();
