use ffmpeg_next::codec::context::Context;
use ffmpeg_next::decoder::video::Video;
use ffmpeg_next::format::context::Input;
use ffmpeg_next::frame::Video as VideoFrame;
use ffmpeg_next::media::Type;
use ffmpeg_next::Packet;

use std::collections::VecDeque;
use std::sync::{mpsc, Arc, Condvar, Mutex};

use sdl2::event::Event;
use sdl2::pixels::Color;
use std::time::Duration;

// A receiver that will receive a message on a channel,
// prompting it to stop.
type StopReceiver = Arc<Mutex<mpsc::Receiver<bool>>>;

/// A fixed-sized, thread-safe queue
struct FixedQueue<T> {
    // Used to notify when data has been read from the queue
    read_var: Condvar,

    // Used to indicate whether data has been read from the queue
    has_read: Mutex<bool>,

    // Used to indicate whether we should wait for the queue
    // to be read from. Defaults to true
    wait: Mutex<bool>,

    data: Mutex<VecDeque<T>>,
    max_size: usize,
}

impl<T> FixedQueue<T> {
    pub fn new(max_size: usize) -> Self {
        Self {
            max_size,
            read_var: Condvar::new(),
            wait: Mutex::new(true),
            has_read: Mutex::new(false),
            data: Mutex::new(VecDeque::new()),
        }
    }

    pub fn toggle_wait(&self) {
        let mut wait = self.wait.lock().unwrap();
        *wait = !*wait;
    }

    pub fn read(&self) -> Option<T> {
        // Signal that we've read from the queue
        let mut has_read = self.has_read.lock().unwrap();
        *has_read = true;
        self.read_var.notify_all();

        self.data.lock().unwrap().remove(0)
    }

    fn wait_for_read(&self) {
        let size = self.data.lock().unwrap().len();
        if size < self.max_size {
            return;
        }

        // We can't have more than `self.max_size` items, so we
        // need to sleep until we've read from the q
        let timeout = Duration::from_millis(100);
        let mut has_read = self.has_read.lock().unwrap();
        while !*has_read {
            // Stop waiting if we decide to, in a separate
            // scope so we don't have to wait for the timeout
            // to elapse
            {
                let should_still_wait = self.wait.lock().unwrap();
                if !*should_still_wait {
                    break;
                }
            }

            // Wait
            (has_read, _) = self.read_var.wait_timeout(has_read, timeout).unwrap();
        }
        *has_read = false;
    }

    pub fn write(&self, value: T) {
        self.wait_for_read();
        self.data.lock().unwrap().push_back(value);
    }
}

struct VideoDecoder {
    stream_index: usize,
    decoder: Mutex<Video>,
    packet_queue: FixedQueue<Packet>,
    frame_queue: FixedQueue<VideoFrame>,
}

impl VideoDecoder {
    pub fn new(context: &Input) -> Self {
        let media = context.streams().best(Type::Video).unwrap();
        let codec_context = Context::from_parameters(media.parameters()).unwrap();
        let decoder = codec_context.decoder().video().unwrap();
        Self {
            stream_index: media.index(),
            packet_queue: FixedQueue::new(25),
            frame_queue: FixedQueue::new(25),
            decoder: Mutex::new(decoder),
        }
    }

    fn flush(&self) {
        self.decoder.lock().unwrap().send_eof().unwrap();
        let mut frame = VideoFrame::empty();
        while self
            .decoder
            .lock()
            .unwrap()
            .receive_frame(&mut frame)
            .is_ok()
        {}
    }

    pub fn decode(&self, receiver: StopReceiver) {
        loop {
            // Stop when we receive something on the channel
            if receiver.lock().unwrap().try_recv().is_ok() {
                self.flush();
                break;
            }

            let packet = self.packet_queue.read();
            if packet.is_none() {
                continue;
            }

            self.decoder
                .lock()
                .unwrap()
                .send_packet(&packet.unwrap())
                .unwrap();

            let mut frame = VideoFrame::empty();
            while self
                .decoder
                .lock()
                .unwrap()
                .receive_frame(&mut frame)
                .is_ok()
            {
                self.frame_queue.write(frame.clone());
            }
        }
    }
}

fn main() {
    // Construct a channel used for signaling termination to the decoding threads.
    // The main thread will use the `stop_sender` to send a message when it's time
    // to stop the program. The `stop_receiver` in the decoding threads will
    // receive the message, which will then prompt them to exit their decoding
    // loop and terminate
    let (stop_sender, rx) = mpsc::channel::<bool>();
    let stop_receiver = Arc::new(Mutex::new(rx));

    ffmpeg_next::init().unwrap();
    let path = "/home/aabiji/Videos/sync-test.mp4";
    let mut format_context = ffmpeg_next::format::input(path).unwrap();

    let video_decoder = Arc::new(VideoDecoder::new(&format_context));
    let video_decoding_thread = {
        let vdecoder = Arc::clone(&video_decoder);
        let receiver = Arc::clone(&stop_receiver);
        std::thread::spawn(move || {
            vdecoder.decode(receiver);
        })
    };

    let decoding_thread = {
        let vdecoder = Arc::clone(&video_decoder);
        let receiver = Arc::clone(&stop_receiver);

        std::thread::spawn(move || {
            for (stream, packet) in format_context.packets() {
                // Stop when we receive something on the channel
                if receiver.lock().unwrap().try_recv().is_ok() {
                    break;
                }

                if stream.index() == vdecoder.stream_index {
                    vdecoder.packet_queue.write(packet);
                }
            }
        })
    };

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
                Event::Quit { .. } => {
                    video_decoder.packet_queue.toggle_wait();
                    video_decoder.frame_queue.toggle_wait();
                    stop_sender.send(true).unwrap(); // Send for decoding_thread
                    stop_sender.send(true).unwrap(); // Send for video_decoding_thread
                    break 'running;
                }
                _ => {}
            }
        }

        canvas.clear();
        canvas.set_draw_color(Color::RGB(255, 255, 255));

        if let Some(frame) = video_decoder.frame_queue.read() {
            println!("Video frame ({}x{})", frame.width(), frame.height());
        }

        canvas.present();
        std::thread::sleep(fps);
    }

    video_decoding_thread.join().unwrap();
    decoding_thread.join().unwrap();
}
