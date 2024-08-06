use ffmpeg_next as ffmpeg;

// we could store audio and video decoder in the same struct
// audio and video could be seperate -- then we could put scaling in their respective structs

struct Decoder {
    index: usize,
    decoder: ffmpeg::decoder::Decoder,
}

impl Decoder {
    pub fn new(is_video: bool, context: ffmpeg::format::context::Input) -> Self {
        let media = context.streams().best(ffmpeg::media::Type::Video).unwrap();
        let index = media.index();
        let codec_context =
            ffmpeg::codec::context::Context::from_parameters(media.parameters()).unwrap();
        let decoder = codec_context.decoder();
        Self { index, decoder }
    }

    pub fn decode(&mut self, packet: &ffmpeg::frame::Packet) {
        self.decoder.send_packet(packet).unwrap();

        let mut frame = ffmpeg::frame::Video::empty();
        while self.decoder.receive_frame(&mut frame).is_ok() {
            println!("Video frame: {}x{}", frame.width(), frame.height());
        }
    }
}

fn main() {
    ffmpeg::init().unwrap();

    let path = "/home/aabiji/Videos/sync-test.mp4";
    let mut format_context = ffmpeg::format::input(path).unwrap();
    let mut decoder = Decoder::new(true);

    for (stream, packet) in format_context.packets() {
        if stream.index() == decoder.index {
            decoder.decode(&packet);
        }
    }

    println!("{path}");
}
