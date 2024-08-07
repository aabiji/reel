use ffmpeg_next::codec::context::Context;
use ffmpeg_next::decoder::video::Video;
use ffmpeg_next::format::context::Input;
use ffmpeg_next::frame;
use ffmpeg_next::media::Type;
use ffmpeg_next::Packet;

// Current architecture:
// Main decoding thread that pushes packets to their respective queues
// Video decoder runs on a sepearte thread, fetches and decodes packets
// from the queue and pushes frames unto a queue.
// Audio decoder does the same thing.
// Is there a better way to structure a video player?
// TODO: multi threaded decoding
// TODO: fixed sized queue

struct VideoDecoder {
    stream_index: usize,
    decoder: Video,
    packet_queue: Vec<Packet>,
}

impl VideoDecoder {
    pub fn new(context: &Input) -> Self {
        let media = context.streams().best(Type::Video).unwrap();
        let codec_context = Context::from_parameters(media.parameters()).unwrap();
        let decoder = codec_context.decoder().video().unwrap();
        Self {
            stream_index: media.index(),
            packet_queue: Vec::new(),
            decoder,
        }
    }

    fn push_packet_queue(&mut self, packet: Packet) {
        self.packet_queue.push(packet);

        //println!("{}", self.packet_queue.len());
    }

    fn decode(&mut self) {
        loop {
            let value = self.packet_queue.pop(); // pop sounds dodgy -- we should be reading from the front
            if let Some(packet) = value {
                self.decoder.send_packet(&packet).unwrap();

                let mut frame = frame::Video::empty();
                while self.decoder.receive_frame(&mut frame).is_ok() {
                    println!("Video frame: {}x{}", frame.width(), frame.height());
                }
            }
        }
    }
}

fn main() {
    ffmpeg_next::init().unwrap();

    let path = "/home/aabiji/Videos/sync-test.mp4";
    let mut format_context = ffmpeg_next::format::input(path).unwrap();
    let mut video_decoder = VideoDecoder::new(&format_context);

    for (stream, packet) in format_context.packets() {
        if stream.index() == video_decoder.stream_index {
            video_decoder.push_packet_queue(packet);
        }
    }

    println!("{path}");
}
