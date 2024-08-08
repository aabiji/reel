use ffmpeg_next::codec::context::Context;
use ffmpeg_next::decoder::video::Video;
use ffmpeg_next::format::context::Input;
use ffmpeg_next::frame;
use ffmpeg_next::media::Type;
use ffmpeg_next::Packet;

use std::collections::VecDeque;
use std::sync::{Arc, Condvar, Mutex};
use std::thread;

use sdl2::event::Event;
use sdl2::pixels::Color;
use std::time::Duration;

/// Fixed sized, thread safe queue
struct FixedQueue<T> {
    read_var: Condvar, // Notifies on data read
    has_read: Mutex<bool>,

    write_var: Condvar, // Notifies on data write
    has_written: Mutex<bool>,

    data: Mutex<VecDeque<T>>,
    max_size: usize,
}

impl<T> FixedQueue<T> {
    pub fn new(max_size: usize) -> Self {
        Self {
            read_var: Condvar::new(),
            has_read: Mutex::new(false),
            write_var: Condvar::new(),
            has_written: Mutex::new(false),
            data: Mutex::new(VecDeque::new()),
            max_size,
        }
    }

    pub fn read(&self) -> T {
        // There always needs to be something for us to read,
        // so we'll sleep until we've written to the queue
        if self.data.lock().unwrap().len() == 0 {
            let mut has_written = self.has_written.lock().unwrap();
            while !*has_written {
                has_written = self.write_var.wait(has_written).unwrap();
            }
            *has_written = false;
        }

        // Signal that we've read from the queue
        let mut has_read = self.has_read.lock().unwrap();
        *has_read = true;
        self.read_var.notify_one();

        let value = self.data.lock().unwrap().remove(0).unwrap();
        value
    }

    pub fn write(&self, value: T) {
        // We can't have more than `self.max_size` items, so we
        // need to sleep until we've read from the queue
        if self.data.lock().unwrap().len() >= self.max_size {
            let mut has_read = self.has_read.lock().unwrap();
            while !*has_read {
                has_read = self.read_var.wait(has_read).unwrap();
            }
            *has_read = false;
        }

        self.data.lock().unwrap().push_back(value);

        // Signal that we've written to the queue
        let mut has_written = self.has_written.lock().unwrap();
        *has_written = true;
        self.write_var.notify_one();
    }
}

struct VideoDecoder {
    stream_index: usize,
    decoder: Mutex<Video>,
    packet_queue: FixedQueue<Packet>,
}

impl VideoDecoder {
    pub fn new(context: &Input) -> Self {
        let media = context.streams().best(Type::Video).unwrap();
        let codec_context = Context::from_parameters(media.parameters()).unwrap();
        let decoder = codec_context.decoder().video().unwrap();
        Self {
            stream_index: media.index(),
            packet_queue: FixedQueue::new(25),
            decoder: Mutex::new(decoder),
        }
    }

    fn decode(&self) {
        loop {
            let packet = self.packet_queue.read();
            unsafe {
                // An empty packet signifies the end of the stream
                if packet.is_empty() && packet.duration() == -1 {
                    self.flush_decoder();
                    break;
                }
            }

            self.decoder.lock().unwrap().send_packet(&packet).unwrap();

            let mut frame = frame::Video::empty();
            while self
                .decoder
                .lock()
                .unwrap()
                .receive_frame(&mut frame)
                .is_ok()
            {}
        }
    }

    fn flush_decoder(&self) {
        self.decoder.lock().unwrap().send_eof().unwrap();
        let mut frame = frame::Video::empty();
        while self
            .decoder
            .lock()
            .unwrap()
            .receive_frame(&mut frame)
            .is_ok()
        {}
    }
}

fn main() {
    /*
    ffmpeg_next::init().unwrap();

    let path = "/home/aabiji/Videos/sync-test.mp4";
    let mut format_context = ffmpeg_next::format::input(path).unwrap();
    let video_decoder = Arc::new(VideoDecoder::new(&format_context));

    let video_decoding_thread = {
        let decoder = Arc::clone(&video_decoder);
        std::thread::spawn(move || {
            decoder.decode();
        })
    };

    let decoder_thread = {
        std::thread::spawn(move || {
            let mut end_packet = Packet::empty();
            end_packet.set_duration(-1);

            for (stream, packet) in format_context.packets() {
                if stream.index() == video_decoder.stream_index {
                    video_decoder.packet_queue.write(packet);
                }
            }

            // Write end of stream packets
            video_decoder.packet_queue.write(end_packet);
        })
    };

    video_decoding_thread.join().unwrap();
    decoder_thread.join().unwrap();
     */

    let sdl_context = sdl2::init().unwrap();
    let video_subsystem = sdl_context.video().unwrap();

    let width = 800;
    let height = 600;
    let window = video_subsystem
        .window("Reel", width, height)
        .position_centered()
        .opengl()
        .build()
        .unwrap();

    let mut canvas = window.into_canvas().accelerated().build().unwrap();

    let fps = Duration::new(0, 1000000000u32 / 60);
    let mut event_pump = sdl_context.event_pump().unwrap();
    'running: loop {
        for event in event_pump.poll_iter() {
            match event {
                Event::Quit { .. } => break 'running,
                _ => {}
            }
        }

        canvas.clear();
        canvas.set_draw_color(Color::RGB(255, 255, 255));
        canvas.present();
        std::thread::sleep(fps);
    }
}
