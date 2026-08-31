"""
HuBERT Audio Classifier - Gradio App สำหรับ deploy บน Hugging Face Spaces

จำแนกเสียงเป็น 3 คลาส: Plant_Sounds / Normal / Noise
ใช้โมเดล HuBERT ที่ fine-tune แล้ว (เวอร์ชันปกติ fp32 ไม่บีบอัด)

ต้องมีไฟล์ checkpoint "hubert_plant_normal_noise_classifier.pt" อยู่โฟลเดอร์เดียวกับไฟล์นี้
"""

import os
import glob
import numpy as np
import torch
import librosa
import librosa.display
import matplotlib
matplotlib.use("Agg")  # ไม่ต้องใช้ GUI backend บน server
import matplotlib.pyplot as plt
import gradio as gr
from transformers import HubertForSequenceClassification, Wav2Vec2FeatureExtractor

# ---------------------------------------------------------------------------
# 0) อ่านค่า HF_TOKEN จาก Environment Variables (ถ้ามี)
# ---------------------------------------------------------------------------
HF_TOKEN = os.environ.get("HF_TOKEN", None)  # ปลอดภัย 

# รองรับ Hugging Face ZeroGPU (ถ้า Space ตั้ง Hardware เป็น ZeroGPU)
try:
    import spaces
    GPU_DECORATOR = spaces.GPU
except ImportError:
    def GPU_DECORATOR(fn):
        return fn

# ---------------------------------------------------------------------------
# 1) โหลด checkpoint + สร้างโมเดล (โหลดไว้บน CPU ก่อนเสมอ)
# ---------------------------------------------------------------------------
CKPT_PATH = "hubert_plant_normal_noise_classifier.pt"
EXAMPLES_DIR = "examples"

print("Loading checkpoint:", CKPT_PATH)
checkpoint = torch.load(CKPT_PATH, map_location="cpu")

MODEL_NAME = checkpoint["model_name"]
LABEL2ID = checkpoint["label2id"]
ID2LABEL = checkpoint["id2label"]
SAMPLE_RATE = checkpoint["sample_rate"]
MAX_DURATION = checkpoint.get("max_duration", 1.0)

# ดึงสถาปัตยกรรมโมเดลพร้อมส่ง token เพื่อผ่าน Rate Limit
model = HubertForSequenceClassification.from_pretrained(
    MODEL_NAME,
    num_labels=len(LABEL2ID),
    label2id=LABEL2ID,
    id2label=ID2LABEL,
    token=HF_TOKEN
)
model.load_state_dict(checkpoint["model_state_dict"])
model.eval()

feature_extractor = Wav2Vec2FeatureExtractor.from_pretrained(MODEL_NAME, token=HF_TOKEN)

print(f"โหลดโมเดลเรียบร้อย | คลาส: {list(ID2LABEL.values())} | sample_rate={SAMPLE_RATE} | max_duration={MAX_DURATION}s")


# ---------------------------------------------------------------------------
# 2) ฟังก์ชันรัน inference บน GPU (หรือ CPU หากไม่มี GPU)
# ---------------------------------------------------------------------------
@GPU_DECORATOR
def _run_inference(input_values):
    infer_device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model.to(infer_device)
    input_values = input_values.to(infer_device)
    
    with torch.no_grad():
        logits = model(input_values=input_values).logits
        probs = torch.softmax(logits, dim=-1).squeeze(0).cpu().numpy()
        
    if infer_device.type == "cuda":
        torch.cuda.empty_cache()  # คืนหน่วยความจำ GPU ป้องกัน VRAM เต็ม
        
    return probs


# ---------------------------------------------------------------------------
# 3) ฟังก์ชันวิเคราะห์เสียง: ทำนาย + โชว์ waveform/spectrogram
# ---------------------------------------------------------------------------
def analyze_audio(audio_filepath):
    if audio_filepath is None:
        return {}, None, "กรุณาอัพโหลดไฟล์เสียง .wav ก่อนครับ"

    waveform, sr = librosa.load(audio_filepath, sr=SAMPLE_RATE, mono=True)
    wav_np = waveform

    # ตัด/pad ให้ยาวเท่ากับตอนเทรน
    max_len = int(SAMPLE_RATE * MAX_DURATION)
    if len(wav_np) > max_len:
        wav_np = wav_np[:max_len]
    elif len(wav_np) < max_len:
        wav_np = np.pad(wav_np, (0, max_len - len(wav_np)))

    # ---- ทำนาย ----
    inputs = feature_extractor(wav_np, sampling_rate=sr, return_tensors="pt")
    probs = _run_inference(inputs["input_values"])

    pred_id = int(np.argmax(probs))
    pred_label = ID2LABEL[pred_id]
    prob_dict = {ID2LABEL[i]: float(probs[i]) for i in range(len(probs))}

    # ---- waveform + spectrogram ----
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 6))

    ax1.plot(np.linspace(0, len(wav_np) / sr, len(wav_np)), wav_np)
    ax1.set_title("Waveform")
    ax1.set_xlabel("Time (s)")
    ax1.set_ylabel("Amplitude")

    fmax = 8192
    S = librosa.feature.melspectrogram(y=wav_np, sr=sr, n_mels=128, fmax=fmax)
    S_db = librosa.power_to_db(S, ref=np.max)
    img = librosa.display.specshow(S_db, sr=sr, x_axis="time", y_axis="mel", fmax=fmax, ax=ax2)
    ax2.set_title("Mel Spectrogram")
    fig.colorbar(img, ax=ax2, format="%+2.0f dB")

    plt.tight_layout()

    result_text = f"ทำนาย: {pred_label}  (ความมั่นใจ {probs[pred_id] * 100:.2f}%)"
    return prob_dict, fig, result_text


# ---------------------------------------------------------------------------
# 4) ไฟล์ตัวอย่างจากโฟลเดอร์ examples/
# ---------------------------------------------------------------------------
MAX_EXAMPLES_PER_CLASS = 300


def collect_example_files_by_class(base_dir, max_per_class):
    if not os.path.isdir(base_dir):
        return {}

    subfolders = sorted(
        d for d in os.listdir(base_dir) if os.path.isdir(os.path.join(base_dir, d))
    )

    grouped = {}
    if subfolders:
        for sub in subfolders:
            sub_files = sorted(glob.glob(os.path.join(base_dir, sub, "*.wav")))[:max_per_class]
            if sub_files:
                grouped[sub] = sub_files
    else:
        flat_files = sorted(glob.glob(os.path.join(base_dir, "*.wav")))[:max_per_class]
        if flat_files:
            grouped["ทั้งหมด"] = flat_files

    return grouped


examples_by_class = collect_example_files_by_class(EXAMPLES_DIR, MAX_EXAMPLES_PER_CLASS)
total_example_count = sum(len(v) for v in examples_by_class.values())
print(
    f"ใช้ไฟล์ตัวอย่าง {total_example_count} ไฟล์ จาก {len(examples_by_class)} คลาส "
    f"(จำกัดที่ {MAX_EXAMPLES_PER_CLASS} ไฟล์ต่อคลาส) จากโฟลเดอร์ {EXAMPLES_DIR}/"
)


# ---------------------------------------------------------------------------
# 5) Gradio Interface
# ---------------------------------------------------------------------------
demo = gr.Blocks(title="HuBERT Plant Audio Classifier")

with demo:
    gr.Markdown("# 🌱 HuBERT Plant Audio Classifier")
    gr.Markdown(
        f"อัพโหลดไฟล์เสียง .wav ความยาว ~{MAX_DURATION:g} วินาที "
        f"ระบบจะจำแนกออกเป็น {len(ID2LABEL)} คลาส: {list(ID2LABEL.values())}"
    )

    with gr.Row(equal_height=False):
        with gr.Column(scale=1):
            audio_input = gr.Audio(type="filepath", label="อัพโหลดไฟล์เสียง (.wav)")
            with gr.Row():
                clear_btn = gr.ClearButton(value="Clear")
                submit_btn = gr.Button("Submit", variant="primary")

            if examples_by_class:
                gr.Markdown("#### 🎵 หรือเลือกไฟล์ตัวอย่าง")
                class_names_sorted = sorted(examples_by_class.keys())
                first_class = class_names_sorted[0]
                first_choices = [
                    (os.path.basename(fp), fp) for fp in examples_by_class[first_class]
                ]

                with gr.Row():
                    class_dropdown = gr.Dropdown(
                        choices=class_names_sorted,
                        value=first_class,
                        label="คลาส",
                        scale=1,
                    )
                    file_dropdown = gr.Dropdown(
                        choices=first_choices,
                        value=first_choices[0][1] if first_choices else None,
                        label=f"ไฟล์ตัวอย่าง (มี {len(examples_by_class[first_class])} ไฟล์)",
                        scale=2,
                    )
                load_example_btn = gr.Button("โหลดไฟล์ตัวอย่างนี้")

                def _update_file_choices(selected_class):
                    files = examples_by_class.get(selected_class, [])
                    choices = [(os.path.basename(fp), fp) for fp in files]
                    new_value = choices[0][1] if choices else None
                    return gr.update(
                        choices=choices,
                        value=new_value,
                        label=f"ไฟล์ตัวอย่าง (มี {len(files)} ไฟล์)",
                    )

                class_dropdown.change(
                    fn=_update_file_choices, inputs=class_dropdown, outputs=file_dropdown
                )

        with gr.Column(scale=1):
            label_output = gr.Label(num_top_classes=len(ID2LABEL), label="ความน่าจะเป็นแต่ละคลาส")
            plot_output = gr.Plot(label="Waveform + Spectrogram")
            text_output = gr.Textbox(label="ผลการทำนาย")

    outputs = [label_output, plot_output, text_output]
    submit_btn.click(fn=analyze_audio, inputs=audio_input, outputs=outputs)
    audio_input.change(fn=analyze_audio, inputs=audio_input, outputs=outputs)
    clear_btn.add([audio_input] + outputs)

    if examples_by_class:
        load_example_btn.click(fn=lambda fp: fp, inputs=file_dropdown, outputs=audio_input)

if __name__ == "__main__":
    # เปิดใช้งาน Queue เพื่อรองรับผู้ใช้งานหลายคนพร้อมกันโดยไม่ล้ม
    demo.queue()
    demo.launch()