//! Non-blocking Linux evdev reader. Set QEAF_TOUCH_* axis ranges from EVIOCGABS.
use std::fs::OpenOptions;
use std::io;
use std::os::fd::AsRawFd;

#[repr(C)]
#[derive(Clone,Copy,Default)]
struct InputEvent { sec:libc::time_t, usec:libc::suseconds_t, kind:u16, code:u16, value:i32 }
#[derive(Debug,Clone,Copy,PartialEq)]
pub enum Input { TouchDown{x:f32,y:f32}, TouchMove{x:f32,y:f32}, TouchUp{x:f32,y:f32}, Key{code:u16,pressed:bool} }
pub struct Evdev { file:std::fs::File,x_min:i32,x_max:i32,y_min:i32,y_max:i32,x:i32,y:i32,down:bool }
impl Evdev {
 pub fn open(path:&str,x_min:i32,x_max:i32,y_min:i32,y_max:i32)->io::Result<Self>{
  if x_max<=x_min||y_max<=y_min{return Err(io::Error::new(io::ErrorKind::InvalidInput,"invalid touch axis range"));}
  let file=OpenOptions::new().read(true).open(path)?;
  let flags=unsafe{libc::fcntl(file.as_raw_fd(),libc::F_GETFL)};
  if flags<0||unsafe{libc::fcntl(file.as_raw_fd(),libc::F_SETFL,flags|libc::O_NONBLOCK)}<0{return Err(io::Error::last_os_error());}
  Ok(Self{file,x_min,x_max,y_min,y_max,x:x_min,y:y_min,down:false})
 }
 fn normalized(&self)->(f32,f32){
  (((self.x-self.x_min)as f32/(self.x_max-self.x_min)as f32).clamp(0.0,1.0),((self.y-self.y_min)as f32/(self.y_max-self.y_min)as f32).clamp(0.0,1.0))
 }
 pub fn open_auto(path:&str)->io::Result<Self>{
  #[repr(C)] struct AbsInfo{value:i32,minimum:i32,maximum:i32,fuzz:i32,flat:i32,resolution:i32}
  fn axis(fd:i32,code:u8)->io::Result<(i32,i32)>{
   let mut info=AbsInfo{value:0,minimum:0,maximum:0,fuzz:0,flat:0,resolution:0};
   let req=((2u64<<30)|((std::mem::size_of::<AbsInfo>()as u64)<<16)|((b'E' as u64)<<8)|(0x40+code as u64))as libc::c_int;
   if unsafe{libc::ioctl(fd,req,&mut info)}<0{return Err(io::Error::last_os_error());}
   if info.maximum<=info.minimum{return Err(io::Error::new(io::ErrorKind::InvalidData,"evdev axis has invalid range"));}
   Ok((info.minimum,info.maximum))
  }
  let f=OpenOptions::new().read(true).open(path)?;
  let fd=f.as_raw_fd();
  let (xmin,xmax)=axis(fd,53).or_else(|_|axis(fd,0))?;
  let (ymin,ymax)=axis(fd,54).or_else(|_|axis(fd,1))?;
  drop(f);
  Self::open(path,xmin,xmax,ymin,ymax)
 }
 pub fn poll(&mut self)->io::Result<Vec<Input>>{
  let mut out=Vec::new();
  loop{
   let mut ev=InputEvent::default();
   let n=unsafe{libc::read(self.file.as_raw_fd(),&mut ev as *mut _ as *mut libc::c_void,std::mem::size_of::<InputEvent>())};
   if n==0{break;}
   if n<0{let e=io::Error::last_os_error();if e.kind()==io::ErrorKind::WouldBlock||e.raw_os_error()==Some(libc::EINTR){break;}return Err(e);}
   if n as usize!=std::mem::size_of::<InputEvent>(){continue;}
   match(ev.kind,ev.code,ev.value){
    (3,0,v)|(3,53,v)=>self.x=v,
    (3,1,v)|(3,54,v)=>self.y=v,
    (3,57,v)=>{
     let pressed=v>=0;
     if pressed&&!self.down{self.down=true;let(x,y)=self.normalized();out.push(Input::TouchDown{x,y});}
     else if !pressed&&self.down{self.down=false;let(x,y)=self.normalized();out.push(Input::TouchUp{x,y});}
    }
    (1,330,v)=>{
     let pressed=v!=0;
     if pressed&&!self.down{self.down=true;let(x,y)=self.normalized();out.push(Input::TouchDown{x,y});}
     else if !pressed&&self.down{self.down=false;let(x,y)=self.normalized();out.push(Input::TouchUp{x,y});}
    }
    (1,code,v)=>out.push(Input::Key{code,pressed:v!=0}),
    (0,0,_)if self.down=>{let(x,y)=self.normalized();out.push(Input::TouchMove{x,y});}
    _=>{}
   }
  }
  Ok(out)
 }
}
