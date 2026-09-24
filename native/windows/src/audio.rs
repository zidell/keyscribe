use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{SampleFormat, Stream, StreamConfig};
use std::{
    fs,
    path::PathBuf,
    sync::{
        atomic::{AtomicU32, Ordering},
        mpsc::{self, SyncSender},
        Arc,
    },
    thread::{self, JoinHandle},
};

pub struct Recording {
    stream: Option<Stream>,
    sender: Option<SyncSender<Vec<i16>>>,
    writer: Option<JoinHandle<Result<PathBuf, String>>>,
    level: Arc<AtomicU32>,
}

impl Recording {
    pub fn start() -> Result<Self, String> {
        let device = cpal::default_host()
            .default_input_device()
            .ok_or("마이크를 찾을 수 없습니다")?;
        let supported = device.default_input_config().map_err(|e| e.to_string())?;
        let sample_rate = supported.sample_rate().0;
        let channels = supported.channels() as usize;
        let format = supported.sample_format();
        let config: StreamConfig = supported.into();
        let (sender, receiver) = mpsc::sync_channel::<Vec<i16>>(16);
        let level = Arc::new(AtomicU32::new(0));
        let error = |err| eprintln!("KeyScribe audio: {err}");
        let stream = match format {
            SampleFormat::F32 => {
                let sink = sender.clone();
                let meter = level.clone();
                device.build_input_stream(
                    &config,
                    move |data: &[f32], _| {
                        let mut output = Vec::with_capacity(data.len() / channels);
                        let mut peak = 0u32;
                        for frame in data.chunks(channels) {
                            let mono = frame.iter().sum::<f32>() / frame.len() as f32;
                            let sample = (mono.clamp(-1.0, 1.0) * i16::MAX as f32) as i16;
                            peak = peak.max(sample.unsigned_abs() as u32);
                            output.push(sample);
                        }
                        meter.store(peak, Ordering::Relaxed);
                        let _ = sink.try_send(output);
                    },
                    error,
                    None,
                )
            }
            SampleFormat::I16 => {
                let sink = sender.clone();
                let meter = level.clone();
                device.build_input_stream(
                    &config,
                    move |data: &[i16], _| {
                        let mut output = Vec::with_capacity(data.len() / channels);
                        let mut peak = 0u32;
                        for frame in data.chunks(channels) {
                            let sample = (frame.iter().map(|v| *v as i32).sum::<i32>()
                                / frame.len() as i32)
                                as i16;
                            peak = peak.max(sample.unsigned_abs() as u32);
                            output.push(sample);
                        }
                        meter.store(peak, Ordering::Relaxed);
                        let _ = sink.try_send(output);
                    },
                    error,
                    None,
                )
            }
            SampleFormat::U16 => {
                let sink = sender.clone();
                let meter = level.clone();
                device.build_input_stream(
                    &config,
                    move |data: &[u16], _| {
                        let mut output = Vec::with_capacity(data.len() / channels);
                        let mut peak = 0u32;
                        for frame in data.chunks(channels) {
                            let sample = (frame.iter().map(|v| *v as i32 - 32768).sum::<i32>()
                                / frame.len() as i32)
                                as i16;
                            peak = peak.max(sample.unsigned_abs() as u32);
                            output.push(sample);
                        }
                        meter.store(peak, Ordering::Relaxed);
                        let _ = sink.try_send(output);
                    },
                    error,
                    None,
                )
            }
            _ => return Err("지원하지 않는 마이크 샘플 형식입니다".into()),
        }
        .map_err(|e| e.to_string())?;
        let path = crate::debug_log::new_recording_path().map_err(|e| e.to_string())?;
        let writer = thread::spawn(move || write_wav(receiver, sample_rate, path));
        if let Err(error) = stream.play() {
            drop(stream);
            drop(sender);
            if let Ok(Ok(path)) = writer.join() {
                let _ = fs::remove_file(path);
            }
            return Err(error.to_string());
        }
        Ok(Self {
            stream: Some(stream),
            sender: Some(sender),
            writer: Some(writer),
            level,
        })
    }

    pub fn level(&self) -> f32 {
        (self.level.swap(0, Ordering::Relaxed) as f32 / i16::MAX as f32 * 8.0).min(1.0)
    }

    pub fn stop(mut self) -> Result<PathBuf, String> {
        self.stream.take();
        self.sender.take();
        self.writer
            .take()
            .unwrap()
            .join()
            .map_err(|_| "오디오 저장 스레드가 중단됐습니다".to_string())?
    }
}

impl Drop for Recording {
    fn drop(&mut self) {
        self.stream.take();
        self.sender.take();
        if let Some(writer) = self.writer.take() {
            if let Ok(Ok(path)) = writer.join() {
                let _ = fs::remove_file(path);
            }
        }
    }
}

fn write_wav(
    receiver: mpsc::Receiver<Vec<i16>>,
    input_rate: u32,
    path: PathBuf,
) -> Result<PathBuf, String> {
    let spec = hound::WavSpec {
        channels: 1,
        sample_rate: 16_000,
        bits_per_sample: 16,
        sample_format: hound::SampleFormat::Int,
    };
    let mut writer = hound::WavWriter::create(&path, spec).map_err(|e| e.to_string())?;
    let mut phase = 0u64;
    let mut sum = 0i64;
    let mut count = 0i64;
    for chunk in receiver {
        for sample in chunk {
            sum += sample as i64;
            count += 1;
            phase += 16_000;
            while phase >= input_rate as u64 {
                phase -= input_rate as u64;
                let output = if count == 0 {
                    sample
                } else {
                    (sum / count) as i16
                };
                writer.write_sample(output).map_err(|e| e.to_string())?;
                sum = 0;
                count = 0;
            }
        }
    }
    writer.finalize().map_err(|e| e.to_string())?;
    Ok(path)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_path(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("keyscribe-test-{}-{name}.wav", std::process::id()))
    }

    #[test]
    fn writes_speech_rate_wav() {
        let (sender, receiver) = mpsc::sync_channel(1);
        sender.send(vec![100, 100, 200, 200]).unwrap();
        drop(sender);
        let path = write_wav(receiver, 32_000, test_path("speech-rate")).unwrap();
        let reader = hound::WavReader::open(&path).unwrap();
        assert_eq!(reader.spec().sample_rate, 16_000);
        assert_eq!(reader.duration(), 2);
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn writes_low_rate_wav_without_dividing_by_zero() {
        let (sender, receiver) = mpsc::sync_channel(1);
        sender.send(vec![100, 200]).unwrap();
        drop(sender);
        let path = write_wav(receiver, 8_000, test_path("low-rate")).unwrap();
        let reader = hound::WavReader::open(&path).unwrap();
        assert_eq!(reader.duration(), 4);
        fs::remove_file(path).unwrap();
    }
}
