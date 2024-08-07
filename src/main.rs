use ffmpeg_next::codec::context::Context;
use ffmpeg_next::decoder::audio::Audio;
use ffmpeg_next::decoder::video::Video;
use ffmpeg_next::format::context::Input;
use ffmpeg_next::frame;
use ffmpeg_next::media::Type;
use ffmpeg_next::Packet;

struct VideoDecoder {
    stream_index: usize,
    decoder: Video,
}

impl VideoDecoder {
    pub fn new(context: &Input) -> Self {
        let media = context.streams().best(Type::Video).unwrap();
        let codec_context = Context::from_parameters(media.parameters()).unwrap();
        let decoder = codec_context.decoder().video().unwrap();
        Self {
            stream_index: media.index(),
            decoder,
        }
    }

    fn decode(&mut self, packet: &Packet) {
        self.decoder.send_packet(packet).unwrap();

        let mut frame = frame::Video::empty();
        while self.decoder.receive_frame(&mut frame).is_ok() {
            println!("Video frame: {}x{}", frame.width(), frame.height());
        }
    }
}

struct AudioDecoder {
    stream_index: usize,
    decoder: Audio,
}

impl AudioDecoder {
    pub fn new(context: &Input) -> Self {
        let media = context.streams().best(Type::Audio).unwrap();
        let codec_context = Context::from_parameters(media.parameters()).unwrap();
        let decoder = codec_context.decoder().audio().unwrap();
        Self {
            stream_index: media.index(),
            decoder,
        }
    }

    fn decode(&mut self, packet: &Packet) {
        self.decoder.send_packet(packet).unwrap();

        let mut frame = frame::Audio::empty();
        while self.decoder.receive_frame(&mut frame).is_ok() {
            println!("Audio frame!");
        }
    }
}

fn main() {
    ffmpeg_next::init().unwrap();

    let path = "/home/aabiji/Videos/sync-test.mp4";
    let mut format_context = ffmpeg_next::format::input(path).unwrap();
    let mut video_decoder = VideoDecoder::new(&format_context);
    let mut audio_decoder = AudioDecoder::new(&format_context);

    for (stream, packet) in format_context.packets() {
        if stream.index() == video_decoder.stream_index {
            video_decoder.decode(&packet);
        } else if stream.index() == audio_decoder.stream_index {
            audio_decoder.decode(&packet);
        }
    }

    println!("{path}");
}
