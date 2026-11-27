slint::include_modules!();

mod evdev;
mod kms;
mod qeaf_clipboard;

use std::rc::Rc;
use std::time::{Duration, Instant};
use slint::platform::{Platform, PlatformError, WindowAdapter, WindowEvent, PointerEventButton};
use slint::platform::software_renderer::{MinimalSoftwareWindow, RepaintBufferType, Rgb565Pixel};

struct QeafBionicPlatform {
    window: Rc<MinimalSoftwareWindow>,
    kms: std::cell::RefCell<kms::Kms>,
    touch: std::cell::RefCell<evdev::Evdev>,
    started: Instant,
}
impl Platform for QeafBionicPlatform {
    fn create_window_adapter(&self) -> Result<Rc<dyn WindowAdapter>, PlatformError> {
        Ok(self.window.clone())
    }
    fn duration_since_start(&self) -> Duration { self.started.elapsed() }
    fn run_event_loop(&self) -> Result<(), PlatformError> {
        let (width, height) = self.kms.borrow().size();
        let mut frame = vec![Rgb565Pixel(0); width as usize * height as usize];
        self.window.set_size(slint::PhysicalSize::new(width, height));
        self.window.show()?;
        loop {
            slint::platform::update_timers_and_animations();
            let events = self.touch.borrow_mut().poll()
                .map_err(|e| PlatformError::Other(format!("evdev poll: {e}")))?;
            for event in events {
                let to_pos = |x: f32, y: f32| slint::LogicalPosition::new(x * width as f32, y * height as f32);
                let evt = match event {
                    evdev::Input::TouchDown{x,y} => Some(WindowEvent::PointerPressed { position: to_pos(x,y), button: PointerEventButton::Left }),
                    evdev::Input::TouchMove{x,y} => Some(WindowEvent::PointerMoved { position: to_pos(x,y) }),
                    evdev::Input::TouchUp{x,y} => Some(WindowEvent::PointerReleased { position: to_pos(x,y), button: PointerEventButton::Left }),
                    evdev::Input::Key{code,pressed} => key_event(code, pressed),
                };
                if let Some(evt) = evt {
                    self.window.dispatch_event_with_result(evt)
                        .map_err(|e| PlatformError::Other(format!("dispatch input: {e}")))?;
                }
            }
            let mut did_draw = false;
            self.window.draw_if_needed(|renderer| {
                renderer.render(&mut frame, width as usize);
                did_draw = true;
            });
            if did_draw {
                self.kms.borrow_mut().present_rgb565(
                    unsafe { std::slice::from_raw_parts(frame.as_ptr() as *const u16, frame.len()) }
                ).map_err(|e| PlatformError::Other(format!("DRM present: {e:?}")))?;
            }
            if !self.window.is_visible() { break; }
            let delay = slint::platform::duration_until_next_timer_update()
                .unwrap_or(Duration::from_millis(16))
                .min(Duration::from_millis(16))
                .max(Duration::from_millis(1));
            std::thread::sleep(delay);
        }
        Ok(())
    }
    fn set_clipboard_text(&self, text: &str, _clipboard: slint::platform::Clipboard) {
        if let Err(e) = qeaf_clipboard::set(text) { eprintln!("qeaf-clipboard: SET failed: {e}"); }
    }
    fn clipboard_text(&self, _clipboard: slint::platform::Clipboard) -> Option<String> {
        match qeaf_clipboard::get() {
            Ok(v) => v,
            Err(e) => { eprintln!("qeaf-clipboard: GET failed: {e}"); None }
        }
    }
}
fn key_event(code: u16, pressed: bool) -> Option<WindowEvent> {
    let text = match code {
        1 => "Escape", 28 => "\n", 14 => "\u{8}", 57 => " ",
        103 => "ArrowUp", 108 => "ArrowDown", 105 => "ArrowLeft", 106 => "ArrowRight",
        _ => return None,
    };
    Some(if pressed { WindowEvent::KeyPressed { text: text.into() } }
         else { WindowEvent::KeyReleased { text: text.into() } })
}
fn main() {
    let drm_path = std::env::var("QEAF_DRM_DEVICE").unwrap_or_else(|_| "/dev/dri/card0".into());
    let touch_path = std::env::var("QEAF_TOUCH_DEVICE").unwrap_or_else(|_| "/dev/input/event0".into());
    let kms = kms::Kms::open(&drm_path).expect("QEAF DRM/KMS initialization failed");
    let (width, height) = kms.size();
    let calibrated = ["QEAF_TOUCH_X_MIN","QEAF_TOUCH_X_MAX","QEAF_TOUCH_Y_MIN","QEAF_TOUCH_Y_MAX"]
        .iter().any(|k| std::env::var(k).is_ok());
    let touch = if calibrated {
        evdev::Evdev::open(
            &touch_path,
            env_i32("QEAF_TOUCH_X_MIN", 0), env_i32("QEAF_TOUCH_X_MAX", width as i32 - 1),
            env_i32("QEAF_TOUCH_Y_MIN", 0), env_i32("QEAF_TOUCH_Y_MAX", height as i32 - 1)
        )
    } else {
        evdev::Evdev::open_auto(&touch_path)
    }.expect("QEAF evdev initialization/calibration failed");
    let window = MinimalSoftwareWindow::new(RepaintBufferType::ReusedBuffer);
    slint::platform::set_platform(Box::new(QeafBionicPlatform {
        window, kms: std::cell::RefCell::new(kms), touch: std::cell::RefCell::new(touch), started: Instant::now()
    })).expect("QEAF Bionic platform registration failed");
    let ui = MainWindow::new().expect("QEAF Slint UI creation failed");
    ui.run().expect("QEAF Slint event loop failed");
}
fn env_i32(key: &str, default: i32) -> i32 {
    std::env::var(key).ok().and_then(|v| v.parse().ok()).unwrap_or(default)
}
