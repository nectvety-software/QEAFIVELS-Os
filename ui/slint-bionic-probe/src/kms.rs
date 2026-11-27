//! Double-buffered legacy DRM/KMS scanout with vblank-synchronized page flips.
use drm::buffer::{Buffer, DrmFourcc};
use drm::control::{connector, crtc, Device as ControlDevice, Event, PageFlipFlags};
use drm::{Device, control};
use std::fs::{File, OpenOptions};
use std::os::fd::{AsFd, AsRawFd, BorrowedFd};

#[derive(Debug)]
pub enum KmsError { Io(std::io::Error), NoConnector, NoMode, NoCrtc, FlipTimeout }
impl std::fmt::Display for KmsError {
 fn fmt(&self,f:&mut std::fmt::Formatter<'_>)->std::fmt::Result { match self {
  Self::Io(e)=>write!(f,"DRM I/O: {e}"), Self::NoConnector=>write!(f,"no connected DRM connector"),
  Self::NoMode=>write!(f,"connected connector has no mode"), Self::NoCrtc=>write!(f,"no compatible CRTC"),
  Self::FlipTimeout=>write!(f,"page-flip completion timeout"),
 }}
}
impl std::error::Error for KmsError {}
impl From<std::io::Error> for KmsError { fn from(e:std::io::Error)->Self{Self::Io(e)} }
struct Card(File);
impl AsFd for Card { fn as_fd(&self)->BorrowedFd<'_>{self.0.as_fd()} }
impl Device for Card {}
impl ControlDevice for Card {}

struct Scanout { buffer:control::dumbbuffer::DumbBuffer, fb:control::framebuffer::Handle }
pub struct Kms {
 card:Card, scanouts:[Scanout;2], active:usize, crtc:crtc::Handle, connector:connector::Handle,
 old_fb:Option<control::framebuffer::Handle>, old_mode:Option<control::Mode>, old_pos:(u32,u32),
 width:u32,height:u32,pitch:u32, flip_sequence:u32,
}
impl Kms {
 pub fn open(path:&str)->Result<Self,KmsError>{
  let card=Card(OpenOptions::new().read(true).write(true).open(path)?);
  card.acquire_master_lock()?;
  let res=card.resource_handles()?;
  let conn=res.connectors().iter().filter_map(|h|card.get_connector(*h,true).ok())
   .find(|c|c.state()==connector::State::Connected).ok_or(KmsError::NoConnector)?;
  let mode=*conn.modes().first().ok_or(KmsError::NoMode)?;
  let mut chosen=None;
  for encoder in conn.encoders(){ if let Ok(info)=card.get_encoder(*encoder){
   if let Some(h)=res.filter_crtcs(info.possible_crtcs()).first(){ chosen=card.get_crtc(*h).ok(); if chosen.is_some(){break;} }
  }}
  let ci=chosen.ok_or(KmsError::NoCrtc)?;
  let (w,h)=mode.size(); let width:u32=w.into(); let height:u32=h.into();
  let mut make=||->Result<Scanout,KmsError>{
   let b=card.create_dumb_buffer((w.into(),h.into()),DrmFourcc::Rgb565,16)?;
   let fb=card.add_framebuffer(&b,16,16)?;
   Ok(Scanout{buffer:b,fb})
  };
  let first=make()?; let second=make()?;
  let pitch=first.buffer.pitch();
  card.set_crtc(ci.handle(),Some(first.fb),(0,0),&[conn.handle()],Some(mode))?;
  Ok(Self{card,scanouts:[first,second],active:0,crtc:ci.handle(),connector:conn.handle(),
   old_fb:ci.framebuffer(),old_mode:ci.mode(),old_pos:ci.position(),width,height,pitch,flip_sequence:0})
 }
 pub fn size(&self)->(u32,u32){(self.width,self.height)}
 pub fn present_rgb565(&mut self,pixels:&[u16])->Result<(),KmsError>{
  let expected=self.width as usize*self.height as usize;
  if pixels.len()!=expected{return Err(KmsError::Io(std::io::Error::new(std::io::ErrorKind::InvalidInput,"pixel buffer dimensions mismatch")));}
  let back=1-self.active;
  {
   let scan=&mut self.scanouts[back];
   let mut map=self.card.map_dumb_buffer(&mut scan.buffer)?;
   let dst=map.as_mut();
   for y in 0..self.height as usize {
    let src=y*self.width as usize; let row=y*self.pitch as usize;
    for x in 0..self.width as usize { let b=pixels[src+x].to_ne_bytes(); dst[row+x*2]=b[0];dst[row+x*2+1]=b[1]; }
    let used=self.width as usize*2;
    if self.pitch as usize>used {dst[row+used..row+self.pitch as usize].fill(0);}
   }
  }
  self.card.page_flip(self.crtc,self.scanouts[back].fb,PageFlipFlags::EVENT,None)?;
  let mut pfd=libc::pollfd{fd:self.card.as_fd().as_raw_fd(),events:libc::POLLIN,revents:0};
  let ready=unsafe{libc::poll(&mut pfd,1,1000)};
  if ready==0{return Err(KmsError::FlipTimeout);}
  if ready<0{return Err(KmsError::Io(std::io::Error::last_os_error()));}
  let mut completed=false;
  for event in self.card.receive_events()? { if let Event::PageFlip(ev)=event { if ev.crtc==self.crtc {self.flip_sequence=ev.frame;completed=true;} } }
  if !completed{return Err(KmsError::Io(std::io::Error::new(std::io::ErrorKind::InvalidData,"DRM readable without matching page-flip completion")));}
  self.active=back;
  Ok(())
 }
 pub fn flip_sequence(&self)->u32{self.flip_sequence}
}
impl Drop for Kms {
 fn drop(&mut self){
  let _=self.card.set_crtc(self.crtc,self.old_fb,self.old_pos,&[self.connector],self.old_mode);
  for s in &self.scanouts {let _=self.card.destroy_framebuffer(s.fb);let _=self.card.destroy_dumb_buffer(s.buffer);}
  let _=self.card.release_master_lock();
 }
}
