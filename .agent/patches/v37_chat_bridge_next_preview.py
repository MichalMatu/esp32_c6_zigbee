from pathlib import Path
import sys

root = Path(sys.argv[1])


def replace(path: str, old: str, new: str) -> None:
    target = root / path
    text = target.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


replace(
    "chat_bridge/service_worker.js",
    "async function getBridgeState() {\n  await stateQueue;\n  return loadStoredState();\n}\n",
    "async function getBridgeState() {\n  await stateQueue;\n  return loadStoredState();\n}\n\nasync function getScheduleSnapshot(state) {\n  const alarms = await chrome.alarms.getAll();\n  const alarmByName = new Map(alarms.map((alarm) => [alarm.name, alarm]));\n  return Object.fromEntries(\n    Object.values(state.conversations).map((conversation) => {\n      const alarm = alarmByName.get(alarmName(conversation.id));\n      const rawWhen = alarm?.scheduledTime ?? alarm?.when;\n      const when = Number(rawWhen);\n      return [\n        conversation.id,\n        {\n          scheduled: Number.isFinite(when),\n          nextRunAt: Number.isFinite(when) ? new Date(when).toISOString() : null\n        }\n      ];\n    })\n  );\n}\n",
)
replace(
    "chat_bridge/service_worker.js",
    "      const state = await getBridgeState();\n      const runtime = await loadRuntimeConfig(state);\n      sendResponse({ state, runtime });",
    "      const state = await getBridgeState();\n      const [runtime, schedules] = await Promise.all([\n        loadRuntimeConfig(state),\n        getScheduleSnapshot(state)\n      ]);\n      sendResponse({ state, runtime, schedules });",
)

replace(
    "chat_bridge/service_worker.test.js",
    "    create(name, info) {\n      alarms.set(name, { name, ...clone(info) });\n    },",
    "    create(name, info) {\n      const alarm = { name, ...clone(info) };\n      if (Number.isFinite(Number(info.when))) alarm.scheduledTime = Number(info.when);\n      alarms.set(name, alarm);\n    },",
)
replace(
    "chat_bridge/service_worker.test.js",
    "  assert.ok(pacedAlarm.when <= beforePacingUpdate + 15 * 60_000 + 2_000);\n",
    "  assert.ok(pacedAlarm.when <= beforePacingUpdate + 15 * 60_000 + 2_000);\n\n  // Popup schedule data must follow the actual Chrome alarm, not stale stored state.\n  storage.bridgeState.conversations[bId].nextRunAt = new Date(0).toISOString();\n  response = await sendRuntimeMessage({ type: \"bridge:get-state\" });\n  assert.equal(\n    new Date(response.schedules[bId].nextRunAt).getTime(),\n    pacedAlarm.scheduledTime\n  );\n",
)
replace(
    "chat_bridge/service_worker.test.js",
    "      control: { marker: \"[LAB:NEXT=30s]\" }\n    },\n    { tab: { url: \"https://chatgpt.com/c/b\" } }\n  );\n  assert.equal(response.ok, true);\n  assert.equal(response.seconds, 30);\n  const nextAlarm = alarms.get(`local-agent-chat:${bId}`);\n  assert.ok(nextAlarm.when >= beforeNext + 29_000);\n  assert.ok(nextAlarm.when <= beforeNext + 31_500);",
    "      control: { marker: \"[LAB:NEXT=10m]\" }\n    },\n    { tab: { url: \"https://chatgpt.com/c/b\" } }\n  );\n  assert.equal(response.ok, true);\n  assert.equal(response.seconds, 600);\n  const nextAlarm = alarms.get(`local-agent-chat:${bId}`);\n  assert.ok(nextAlarm.when >= beforeNext + 599_000);\n  assert.ok(nextAlarm.when <= beforeNext + 601_500);\n  response = await sendRuntimeMessage({ type: \"bridge:get-state\" });\n  assert.equal(response.state.conversations[bId].intervalOverrideMinutes, 15);",
)

replace(
    "chat_bridge/popup.js",
    "let latestState = null;\nlet currentTab = null;\n",
    "let latestState = null;\nlet currentTab = null;\nlet countdownTimer = null;\n",
)
replace(
    "chat_bridge/popup.js",
    "function formatTime(value) {\n  if (!value) return \"-\";\n  const date = new Date(value);\n  return Number.isNaN(date.getTime()) ? String(value) : date.toLocaleString();\n}\n",
    "function formatTime(value) {\n  if (!value) return \"-\";\n  const date = new Date(value);\n  return Number.isNaN(date.getTime()) ? String(value) : date.toLocaleString();\n}\n\nfunction formatRemaining(milliseconds) {\n  const totalSeconds = Math.max(0, Math.ceil(Number(milliseconds) / 1000));\n  const hours = Math.floor(totalSeconds / 3600);\n  const minutes = Math.floor((totalSeconds % 3600) / 60);\n  const seconds = totalSeconds % 60;\n  if (hours > 0) return `${hours}h ${minutes}m ${seconds}s`;\n  if (minutes > 0) return `${minutes}m ${seconds}s`;\n  return `${seconds}s`;\n}\n\nfunction updateNextWakeElement(element) {\n  const masterEnabled = element.dataset.masterEnabled === \"true\";\n  const conversationEnabled = element.dataset.conversationEnabled === \"true\";\n  const nextRunAt = element.dataset.nextRunAt || null;\n  if (!masterEnabled) {\n    element.textContent = \"Next: master disabled\";\n    return;\n  }\n  if (!conversationEnabled) {\n    element.textContent = \"Next: paused\";\n    return;\n  }\n  const when = Date.parse(String(nextRunAt || \"\"));\n  if (!Number.isFinite(when)) {\n    element.textContent = \"Next: not scheduled\";\n    return;\n  }\n  const remaining = when - Date.now();\n  const relative = remaining <= 0 ? \"due now\" : `in ${formatRemaining(remaining)}`;\n  element.textContent = `Next: ${relative} · ${formatTime(nextRunAt)}`;\n}\n\nfunction updateCountdowns() {\n  document.querySelectorAll(\"[data-next-run-at]\").forEach(updateNextWakeElement);\n}\n\nfunction restartCountdownTimer() {\n  if (countdownTimer !== null) clearInterval(countdownTimer);\n  updateCountdowns();\n  countdownTimer = setInterval(updateCountdowns, 1000);\n}\n",
)
replace(
    "chat_bridge/popup.js",
    "function renderConversation(conversation) {",
    "function renderConversation(conversation, settings, schedule) {",
)
replace(
    "chat_bridge/popup.js",
    "    Object.assign(document.createElement(\"span\"), {\n      textContent: `Next: ${formatTime(conversation.nextRunAt)}`\n    }),",
    "    (() => {\n      const next = document.createElement(\"span\");\n      next.dataset.nextRunAt = schedule?.nextRunAt || \"\";\n      next.dataset.masterEnabled = settings.masterEnabled ? \"true\" : \"false\";\n      next.dataset.conversationEnabled = conversation.enabled ? \"true\" : \"false\";\n      updateNextWakeElement(next);\n      return next;\n    })(),",
)
replace(
    "chat_bridge/popup.js",
    "function renderConversations(state) {",
    "function renderConversations(state, schedules = {}) {",
)
replace(
    "chat_bridge/popup.js",
    "    elements.conversationList.append(renderConversation(conversation));",
    "    elements.conversationList.append(\n      renderConversation(conversation, state.settings, schedules[conversation.id] || null)\n    );",
)
replace(
    "chat_bridge/popup.js",
    "  renderConversations(latestState);\n  await refreshCurrentTabForm(latestState);",
    "  renderConversations(latestState, response.schedules || {});\n  restartCountdownTimer();\n  await refreshCurrentTabForm(latestState);",
)
replace(
    "chat_bridge/popup.js",
    "refresh().catch((error) => showMessage(`Error: ${error.message}`));",
    "chrome.storage.onChanged.addListener((changes, areaName) => {\n  if (areaName !== \"local\" || !changes.bridgeState) return;\n  refresh().catch((error) => showMessage(`Error: ${error.message}`));\n});\n\nrefresh().catch((error) => showMessage(`Error: ${error.message}`));",
)

replace(
    "chat_bridge/manifest.json",
    '"version": "0.3.0"',
    '"version": "0.3.1"',
)
