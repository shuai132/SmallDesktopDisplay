"use strict";

const { WIDTH, HEIGHT, CURRENT_SLOT } = SmallDisplayProtocol;
const canvas = document.querySelector("#preview");
const context = canvas.getContext("2d", { willReadFrequently: true });
const previewCard = document.querySelector(".preview-card");
const fileInput = document.querySelector("#image-file");
const uploadButton = document.querySelector("#upload-button");
const message = document.querySelector("#message");
const scaleInput = document.querySelector("#scale");
const realtimeInput = document.querySelector("#realtime");
const displayOnlyInput = document.querySelector("#display-only");
const progressiveInput = document.querySelector("#progressive");
const REALTIME_DELAY_MS = 350;
let sourceImage = null;
let sourceUrl = null;
let uploading = false;
let realtimeTimer = null;
let realtimePending = false;
let imageOffsetX = 0;
let imageOffsetY = 0;
let dragPointerId = null;
let dragClientX = 0;
let dragClientY = 0;

function selectedValue(name) { return document.querySelector(`input[name="${name}"]:checked`).value; }
function setMessage(text, type = "") { message.textContent = text; message.className = type; }

function imageGeometry() {
  const rotation = Number(selectedValue("rotate"));
  const fit = selectedValue("fit");
  const extraScale = Number(scaleInput.value) / 100;
  const quarterTurn = rotation === 90 || rotation === 270;
  const availableWidth = quarterTurn ? HEIGHT : WIDTH;
  const availableHeight = quarterTurn ? WIDTH : HEIGHT;
  let drawWidth;
  let drawHeight;
  if (fit === "stretch") {
    drawWidth = availableWidth * extraScale;
    drawHeight = availableHeight * extraScale;
  } else {
    const ratio = fit === "cover"
      ? Math.max(availableWidth / sourceImage.naturalWidth, availableHeight / sourceImage.naturalHeight)
      : Math.min(availableWidth / sourceImage.naturalWidth, availableHeight / sourceImage.naturalHeight);
    drawWidth = sourceImage.naturalWidth * ratio * extraScale;
    drawHeight = sourceImage.naturalHeight * ratio * extraScale;
  }
  return { rotation, fit, drawWidth, drawHeight, renderedWidth: quarterTurn ? drawHeight : drawWidth, renderedHeight: quarterTurn ? drawWidth : drawHeight };
}

function drawPreview() {
  context.save();
  context.fillStyle = "#000";
  context.fillRect(0, 0, WIDTH, HEIGHT);
  if (!sourceImage) { context.restore(); return; }
  const geometry = imageGeometry();
  imageOffsetX = Math.max(-(Math.max(0, geometry.renderedWidth - WIDTH) / 2), Math.min(Math.max(0, geometry.renderedWidth - WIDTH) / 2, imageOffsetX));
  imageOffsetY = Math.max(-(Math.max(0, geometry.renderedHeight - HEIGHT) / 2), Math.min(Math.max(0, geometry.renderedHeight - HEIGHT) / 2, imageOffsetY));
  context.translate(WIDTH / 2 + imageOffsetX, HEIGHT / 2 + imageOffsetY);
  context.rotate((geometry.rotation * Math.PI) / 180);
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.drawImage(sourceImage, -geometry.drawWidth / 2, -geometry.drawHeight / 2, geometry.drawWidth, geometry.drawHeight);
  context.restore();
  document.querySelector("#empty-preview").hidden = true;
  previewCard.classList.add("has-image");
  document.querySelector("#scale-value").value = `${scaleInput.value}%`;
  document.querySelector("#preview-mode").textContent = `${{ contain:"完整", cover:"铺满", stretch:"拉伸" }[geometry.fit]} · ${geometry.rotation}°`;
  scheduleRealtimeUpload();
}

function buildBundle({ realtime = false, save = false } = {}) {
  return SmallDisplayProtocol.buildBundle(context.getImageData(0, 0, WIDTH, HEIGHT), {
    displayOnly: realtime || (!save && displayOnlyInput.checked),
    progressive: realtime || (!save && progressiveInput.checked),
    slot: realtime ? CURRENT_SLOT : Number(selectedValue("slot")),
  });
}

async function refreshState() {
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const state = await response.json();
    const status = document.querySelector("#device-status");
    status.className = "pill online";
    status.textContent = state.uploading ? "正在接收" : state.image_mode ? "图片模式" : "时钟模式";
    document.querySelector("#firmware").textContent = `${state.firmware} · SDDI ${state.protocol} · RGB565`;
    const used = ["A", "B", "C"].filter((_, index) => state.valid_slots & (1 << index));
    document.querySelector("#slot-summary").textContent = `当前 ${String.fromCharCode(65 + state.active_slot)} · ${used.join("/") || "无图片"}`;
  } catch (error) {
    document.querySelector("#device-status").className = "pill";
    document.querySelector("#device-status").textContent = "连接失败";
  }
}

function updateUploadButton() {
  if (uploading) return;
  uploadButton.disabled = !sourceImage;
  document.querySelector("#upload-label").textContent = sourceImage ? (realtimeInput.checked ? "保存当前画面" : displayOnlyInput.checked ? "仅显示" : "显示并保存") : "请先选择图片";
  document.querySelector("#upload-detail").textContent = realtimeInput.checked ? "实时预览不会写入 Flash" : progressiveInput.checked ? "按 240 行渐进刷新" : "校验临时文件后刷新";
}

function scheduleRealtimeUpload(delay = REALTIME_DELAY_MS) {
  if (!realtimeInput.checked || !sourceImage) return;
  realtimePending = true;
  if (uploading || realtimeTimer !== null) return;
  realtimeTimer = setTimeout(() => { realtimeTimer = null; realtimePending = false; uploadImage({ realtime: true }); }, delay);
}

async function uploadImage({ realtime = false, save = false } = {}) {
  if (!sourceImage || uploading) return;
  uploading = true;
  uploadButton.disabled = true;
  uploadButton.classList.add("uploading");
  setMessage("正在生成 240×240 RGB565 数据…");
  try {
    await new Promise((resolve) => setTimeout(resolve, 0));
    const bundle = buildBundle({ realtime, save });
    setMessage("正在发送到设备…");
    const response = await fetch("/api/images", { method:"POST", headers:{ "Content-Type":"application/octet-stream" }, body:bundle });
    const result = await response.json();
    if (!response.ok || !result.ok) throw new Error(result.status || `HTTP ${response.status}`);
    setMessage(result.status === "STORED" ? "图片已显示并保存。" : "图片已显示，本次未写入存储。", "success");
    if (!realtime) await refreshState();
  } catch (error) {
    setMessage(`上传失败：${error.message}`, "error");
  } finally {
    uploading = false;
    uploadButton.classList.remove("uploading");
    updateUploadButton();
    if (realtimePending) scheduleRealtimeUpload(0);
  }
}

async function loadImage(file) {
  if (!file) return;
  if (sourceUrl) URL.revokeObjectURL(sourceUrl);
  sourceUrl = URL.createObjectURL(file);
  const image = new Image();
  image.src = sourceUrl;
  try {
    await image.decode();
    sourceImage = image;
    imageOffsetX = 0;
    imageOffsetY = 0;
    drawPreview();
    updateUploadButton();
    setMessage(`${file.name} · ${image.naturalWidth} × ${image.naturalHeight}`);
  } catch (error) { setMessage("无法读取图片，请改用 JPEG 或 PNG。", "error"); }
}

function updateOptions() {
  if (realtimeInput.checked) { displayOnlyInput.checked = true; progressiveInput.checked = true; }
  displayOnlyInput.disabled = realtimeInput.checked;
  progressiveInput.disabled = realtimeInput.checked;
  document.querySelectorAll('input[name="slot"]').forEach((input) => { input.disabled = displayOnlyInput.checked && input.value !== String(CURRENT_SLOT); });
  if (displayOnlyInput.checked) document.querySelector(`input[name="slot"][value="${CURRENT_SLOT}"]`).checked = true;
  updateUploadButton();
  scheduleRealtimeUpload();
}

function startDrag(event) { if (!sourceImage || dragPointerId !== null) return; dragPointerId = event.pointerId; dragClientX = event.clientX; dragClientY = event.clientY; canvas.setPointerCapture(event.pointerId); }
function moveDrag(event) { if (event.pointerId !== dragPointerId) return; const bounds = canvas.getBoundingClientRect(); imageOffsetX += (event.clientX - dragClientX) * WIDTH / bounds.width; imageOffsetY += (event.clientY - dragClientY) * HEIGHT / bounds.height; dragClientX = event.clientX; dragClientY = event.clientY; drawPreview(); event.preventDefault(); }
function stopDrag(event) { if (event.pointerId === dragPointerId) dragPointerId = null; }
async function control(path) { try { const response = await fetch(path, { method:"POST", body:"" }); if (!response.ok) throw new Error(`HTTP ${response.status}`); await refreshState(); } catch (error) { setMessage(`设备操作失败：${error.message}`, "error"); } }

fileInput.addEventListener("change", () => loadImage(fileInput.files[0]));
document.querySelector("#empty-preview").addEventListener("click", () => fileInput.click());
canvas.addEventListener("pointerdown", startDrag); canvas.addEventListener("pointermove", moveDrag); canvas.addEventListener("pointerup", stopDrag); canvas.addEventListener("pointercancel", stopDrag);
document.querySelectorAll('input[name="fit"],input[name="rotate"]').forEach((input) => input.addEventListener("change", drawPreview));
scaleInput.addEventListener("input", drawPreview);
document.querySelector("#reset-button").addEventListener("click", () => { document.querySelector('input[name="fit"][value="contain"]').checked = true; document.querySelector('input[name="rotate"][value="0"]').checked = true; scaleInput.value = "100"; imageOffsetX = 0; imageOffsetY = 0; drawPreview(); });
realtimeInput.addEventListener("change", updateOptions); displayOnlyInput.addEventListener("change", updateOptions); progressiveInput.addEventListener("change", updateUploadButton);
uploadButton.addEventListener("click", () => uploadImage({ save: realtimeInput.checked }));
document.querySelector("#toggle-mode").addEventListener("click", () => control("/api/mode/toggle"));
document.querySelector("#next-slot").addEventListener("click", () => control("/api/slots/next"));
document.querySelector("#refresh-state").addEventListener("click", refreshState);
context.fillStyle = "#000"; context.fillRect(0, 0, WIDTH, HEIGHT); updateOptions(); refreshState();
