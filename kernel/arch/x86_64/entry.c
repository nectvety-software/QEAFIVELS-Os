#include "../../include/vqeaf/boot_protocol.h"
#include "../../include/vqeaf/serial.h"
#include "../../include/vqeaf/rtc.h"
#include "../../include/vqeaf/memory.h"
#include "../../include/vqeaf/scheduler.h"
#include "../../include/vqeaf/arch_x86_64.h"
#include "../../../bootloader/core/efi_minimal.h"
#include "../../../kernel/include/vqeaf/kheap.h"
#include "../../../kernel/include/vqeaf/vm.h"
#include "../../../kernel/include/vqeaf/elf.h"
#include "../../../user/hello_elf.h"
#include "../../../system/core/include/vqeaf/input.h"
#include "../../../system/core/include/vqeaf/power_menu.h"
#include "../../../system/core/include/vqeaf/driver.h"
#include "../../../system/core/include/vqeaf/capability.h"
#include "../../../system/core/include/vqeaf/storage.h"
#include "../../../system/core/include/vqeaf/block.h"
#include "../../../system/core/include/vqeaf/power.h"
#include "../../../system/ui/include/vqeaf/ui.h"
#include "../../../system/ui/include/vqeaf/recovery_screen.h"
#include "../../../kernel/include/vqeaf/virtio_blk.h"
#include "../../../kernel/include/vqeaf/virtio_net.h"
#include "../../../kernel/include/vqeaf/ahci.h"
#include "../../../kernel/include/vqeaf/vfs.h"
#include "../../../system/core/include/vqeaf/partition.h"
#include "../../../system/core/include/vqeaf/fs_fat16.h"
extern void vqeaf_sched_enter_first(uint64_t rsp);
static EFI_GUID loaded_guid={{0xa1,0x31,0x1b,0x5b,0x62,0x95,0xd2,0x11,0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static CHAR16 bad[]=L"VQEAF kernel: invalid boot protocol or memory map.\r\n";
typedef EFI_STATUS (EFIAPI *GET_MAP)(UINTN*,void*,UINTN*,UINTN*,uint32_t*);
typedef EFI_STATUS (EFIAPI *ALLOC_POOL)(uint32_t,UINTN,void**);
typedef EFI_STATUS (EFIAPI *FREE_POOL)(void*);
typedef EFI_STATUS (EFIAPI *EXIT_BOOT)(EFI_HANDLE,UINTN);
static void serial_u64(uint64_t v){
 char b[21];unsigned n=0;
 if(!v){vqeaf_serial_char('0');return;}
 while(v&&n<20){b[n++]=(char)('0'+(v%10));v/=10;}
 while(n)vqeaf_serial_char(b[--n]);
}
static void stop(void){for(;;)__asm__ volatile("cli; hlt");}
/* Interrupt-safe one-line print for concurrent tasks. */
static void task_print(const char *a,const char *b,uint64_t value){
 char digits[21];unsigned n=0;
 while(value&&n<20){digits[n++]=(char)('0'+(value%10));value/=10;}
 __asm__ volatile("cli");
 vqeaf_serial_write(a);vqeaf_serial_write(b);
 while(n)vqeaf_serial_char(digits[--n]);
 vqeaf_serial_write("\r\n");
 __asm__ volatile("sti");
}
static void task_counter(void *arg){
 uint64_t id=(uint64_t)arg,count=0;
 task_print(id==1?"T1":"T2"," started",0);
 for(;;){
  count++;
  if((count&31u)==0){
   void *p=vqeaf_kmalloc(64);
   if(p)vqeaf_kfree(p);
   task_print(id==1?"T1":"T2"," alive, count=",count);
  }
  __asm__ volatile("hlt");
 }
}
/* ---- power menu + volume-key demo (kernel task over the input service) ---- */
static VqeafPowerMenu menu;
static VqeafPowerManager power_manager;
static uint32_t volume=50;
/* Serial markers make the display power state observable from the host; the
 * wake line is printed before the state change so the test can order events. */
static void power_log(const char *line){
 __asm__ volatile("cli");
 vqeaf_serial_write(line);
 __asm__ volatile("sti");
}
static void display_power_changed(void *ctx,VqeafDisplayPowerState state){
 (void)ctx;
 vqeaf_ui_set_blanked(state!=VQEAF_POWER_ACTIVE);
 power_log(state==VQEAF_POWER_ACTIVE?"POWER: display=active\r\n":state==VQEAF_POWER_DIMMED?"POWER: display=dimmed\r\n":"POWER: display=off\r\n");
}
static void note_user_activity(void){
 uint64_t now=vqeaf_timer_ticks()*10u;
 if(power_manager.suspended)power_log("POWER: wake from display-off\r\n");
 vqeaf_power_set_suspended(&power_manager,0);vqeaf_power_user_activity(&power_manager,now);
}
static void init_core_services(int framebuffer_ready){
 vqeaf_driver_reset();vqeaf_cap_reset();vqeaf_block_reset();vqeaf_storage_reset();
 vqeaf_cap_set("display.framebuffer",framebuffer_ready?VQEAF_CAP_READY:VQEAF_CAP_UNSUPPORTED,0,0);
 vqeaf_cap_set("display.backlight",VQEAF_CAP_UNSUPPORTED,0,0);
 vqeaf_cap_set("storage.vfs",VQEAF_CAP_READY,0,0);
 vqeaf_cap_set("storage.mount_namespace",VQEAF_CAP_READY,0,0);
 vqeaf_cap_set("storage.block",VQEAF_CAP_UNSUPPORTED,0,0);
 vqeaf_cap_set("storage.fat16",VQEAF_CAP_UNSUPPORTED,0,0);
 vqeaf_cap_set("input.serial",VQEAF_CAP_PRESENT,0,0);
 vqeaf_cap_set("power.screen_timeout",framebuffer_ready?VQEAF_CAP_READY:VQEAF_CAP_UNSUPPORTED,0,0);
 if(vqeaf_power_init(&power_manager,VQEAF_TIMEOUT_1M,framebuffer_ready,vqeaf_timer_ticks()*10u)!=0)vqeaf_panic("Power manager init failed",0);
 vqeaf_power_set_blank_callback(&power_manager,display_power_changed,0);
 vqeaf_serial_write("CORE: driver registry, capabilities, storage namespace and power manager ready\r\n");
}
static void menu_print(const char *line,const char *detail){
 __asm__ volatile("cli");
 vqeaf_serial_write(line);
 if(detail)vqeaf_serial_write(detail);
 vqeaf_serial_write("\r\n");
 __asm__ volatile("sti");
}
static void apply_action(VqeafPowerAction a){
 switch(a){
 case VQEAF_PMENU_POWER_OFF:
  menu_print("PMENU: action=PowerOff; system halt","\r\n");
  __asm__ volatile("cli");for(;;)__asm__ volatile("hlt");
 case VQEAF_PMENU_RESTART:
  menu_print("PMENU: action=Restart; 8042 reset","\r\n");
  vqeaf_out8(0x64,0xFE);for(;;)__asm__ volatile("hlt");
 case VQEAF_PMENU_FASTBOOT:
  menu_print("PMENU: action=Fastboot UNSUPPORTED: no bootloader fastboot protocol on this target","\r\n");
  break;
 case VQEAF_PMENU_SLEEP:
  menu_print("PMENU: action=Sleep (display off until user activity)","\r\n");
  power_manager.state=VQEAF_POWER_DISPLAY_OFF;vqeaf_power_set_suspended(&power_manager,1);display_power_changed(0,VQEAF_POWER_DISPLAY_OFF);
  break;
 default:break;
 }
}
static void feed_button(VqeafKeyCode code,VqeafKeyGesture gesture){
 VqeafInputEvent e={code,gesture};
 if(vqeaf_input_push(&e)==-2)menu_print("INPUT: queue full","\r\n");
}
static void feed_phone(VqeafKeyCode code){
 VqeafInputEvent e={code,VQEAF_KEY_TAP};
 if(vqeaf_input_push(&e)==-2)menu_print("INPUT: queue full","\r\n");
}
static VqeafPhoneKey phone_key_for(VqeafKeyCode code){
 switch(code){
 case VQEAF_KEY_NAV_UP:return VQEAF_PHONE_KEY_UP;
 case VQEAF_KEY_NAV_DOWN:return VQEAF_PHONE_KEY_DOWN;
 case VQEAF_KEY_NAV_LEFT:return VQEAF_PHONE_KEY_LEFT;
 case VQEAF_KEY_NAV_RIGHT:return VQEAF_PHONE_KEY_RIGHT;
 case VQEAF_KEY_ENTER:return VQEAF_PHONE_KEY_ENTER;
 case VQEAF_KEY_BACK:return VQEAF_PHONE_KEY_BACK;
 case VQEAF_KEY_QUICK:return VQEAF_PHONE_KEY_QUICK;
 case VQEAF_KEY_LOCK:return VQEAF_PHONE_KEY_LOCK;
 default:return VQEAF_PHONE_KEY_NONE;
 }
}
/* Phone actions that need kernel services: the shell only renders them. */
static void phone_apply_action(int action,uint64_t now){
 switch(action){
 case VQEAF_PHONE_ACTION_TIMEOUT_NEXT:{
  uint32_t next=((uint32_t)power_manager.timeout+1u)%5u;
  vqeaf_power_set_timeout(&power_manager,(VqeafScreenTimeout)next,now);
  menu_print("PHONE: timeout=",vqeaf_phone_timeout_label(next));
 }break;
 case VQEAF_PHONE_ACTION_WAKELOCK_TOGGLE:{
  int active=vqeaf_power_wake_active(&power_manager);
  if(active)vqeaf_power_wake_release(&power_manager,"phone-ui",now);
  else vqeaf_power_wake_lock(&power_manager,"phone-ui",now,0);
  menu_print(active?"PHONE: wakelock=off":"PHONE: wakelock=on","\r\n");
 }break;
 case VQEAF_PHONE_ACTION_POWER_MENU:
  vqeaf_pmenu_open(&menu);
  menu_print("PHONE: power menu opened","\r\n");
  vqeaf_ui_set_power_menu(1,menu.selected);
  break;
 case VQEAF_PHONE_ACTION_SLEEP:
  menu_print("PHONE: sleep","\r\n");
  power_manager.state=VQEAF_POWER_DISPLAY_OFF;
  vqeaf_power_set_suspended(&power_manager,1);
  display_power_changed(0,VQEAF_POWER_DISPLAY_OFF);
  break;
 case VQEAF_PHONE_ACTION_UPDATE:{
  VqeafUpdateView blocked={0,0,0,0,0};
  vqeaf_ui_update_show_view(&blocked);
  menu_print("UPDATE: blocked view shown (no updater service)",0);
 }break;
 case VQEAF_PHONE_ACTION_UPDATE_CLOSE:
  vqeaf_ui_update_hide();menu_print("UPDATE: view hidden",0);break;
 case VQEAF_PHONE_ACTION_FASTBOOT:
  menu_print("PHONE: fastboot mode; W/S select, B back","\r\n");break;
 case VQEAF_PHONE_ACTION_LOCK:
  menu_print("PHONE: lock screen","\r\n");break;
 default:break;
 }
 vqeaf_ui_set_power_status((uint32_t)power_manager.timeout,
  vqeaf_power_wake_active(&power_manager),power_manager.battery_present,
  power_manager.level_permille,power_manager.charging);
}
static void handle_phone_key(VqeafKeyCode code,uint64_t now){
 if(vqeaf_ui_fastboot_active()){
  if(code==VQEAF_KEY_NAV_UP)vqeaf_ui_fastboot_nav(-1);
  else if(code==VQEAF_KEY_NAV_DOWN)vqeaf_ui_fastboot_nav(1);
  else if(code==VQEAF_KEY_ENTER)
   menu_print("FASTBOOT: selected=",vqeaf_fastboot_item_label(vqeaf_ui_fastboot_selected()));
  else if(code==VQEAF_KEY_BACK){vqeaf_ui_show_home();menu_print("PHONE: fastboot closed","\r\n");}
  else menu_print("FASTBOOT: W/S select, B back","\r\n");
  return;
 }
 int before=vqeaf_ui_phone_screen();
 int action=vqeaf_ui_phone_key(phone_key_for(code));
 if(action<0)return;
 phone_apply_action(action,now);
 int after=vqeaf_ui_phone_screen();
 if(after>=0&&after!=before){
  const char *name=after==VQEAF_PHONE_LOCK?"lock":after==VQEAF_PHONE_HOME?"home":
   after==VQEAF_PHONE_QUICK?"quick":"app";
  menu_print("PHONE: view=",name);
 }
}
static void pump_events(void){
 VqeafInputEvent e;
 while(vqeaf_input_pop(&e)==0){
  note_user_activity();
  uint64_t now=vqeaf_timer_ticks()*10u;
  if(menu.state==VQEAF_PMENU_OPEN){
   VqeafInputEvent pe=e;int have=1;
   switch(e.code){
   case VQEAF_KEY_NAV_UP:pe.code=VQEAF_KEY_VOLUME_UP;break;
   case VQEAF_KEY_NAV_DOWN:pe.code=VQEAF_KEY_VOLUME_DOWN;break;
   case VQEAF_KEY_ENTER:pe.code=VQEAF_KEY_POWER;pe.gesture=VQEAF_KEY_TAP;break;
   case VQEAF_KEY_BACK:pe.code=VQEAF_KEY_POWER;pe.gesture=VQEAF_KEY_LONG_PRESS;break;
   case VQEAF_KEY_VOLUME_UP:case VQEAF_KEY_VOLUME_DOWN:case VQEAF_KEY_POWER:break;
   default:have=0;break;
   }
   if(!have)continue;
   int action=vqeaf_pmenu_handle(&menu,&pe);
   if(menu.state==VQEAF_PMENU_OPEN)
    menu_print("PMENU: open highlight=",vqeaf_pmenu_item_name(menu.selected));
   else if(action!=VQEAF_PMENU_NONE)
    menu_print("PMENU: action=",vqeaf_pmenu_item_name((uint32_t)(action-1)));
   vqeaf_ui_set_power_menu(menu.state==VQEAF_PMENU_OPEN,menu.selected);
   if(action!=VQEAF_PMENU_NONE)apply_action((VqeafPowerAction)action);
   continue;
  }
  if(e.code==VQEAF_KEY_POWER){
   int action=vqeaf_pmenu_handle(&menu,&e);
   if(menu.state==VQEAF_PMENU_OPEN){
    menu_print("PMENU: open highlight=",vqeaf_pmenu_item_name(menu.selected));
    vqeaf_ui_set_power_menu(1,menu.selected);
   } else if(action!=VQEAF_PMENU_NONE){
    menu_print("PMENU: action=",vqeaf_pmenu_item_name((uint32_t)(action-1)));
    vqeaf_ui_set_power_menu(0,0);
    apply_action((VqeafPowerAction)action);
   }
   continue;
  }
  if(e.code==VQEAF_KEY_VOLUME_UP){if(volume<100)volume+=10;continue;}
  if(e.code==VQEAF_KEY_VOLUME_DOWN){if(volume>=10)volume-=10;continue;}
  handle_phone_key(e.code,now);
 }
}
/* Serial phone keys: W/A/S/D move, E or Enter selects, B or Backspace goes
 * back, Q quick settings, L lock, P power tap, Shift+P power hold. ESC [ A..D
 * arrow sequences are decoded too. Every byte counts as user activity. */
static int esc_state;
static void feed_serial(char c){
 if(esc_state==1){esc_state=(c=='[')?2:0;return;}
 if(esc_state==2){
  esc_state=0;
  if(c=='A')feed_phone(VQEAF_KEY_NAV_UP);
  else if(c=='B')feed_phone(VQEAF_KEY_NAV_DOWN);
  else if(c=='C')feed_phone(VQEAF_KEY_NAV_RIGHT);
  else if(c=='D')feed_phone(VQEAF_KEY_NAV_LEFT);
  return;
 }
 if(c==0x1B){esc_state=1;return;}
 switch(c){
 case 'w':case 'W':feed_phone(VQEAF_KEY_NAV_UP);break;
 case 's':case 'S':feed_phone(VQEAF_KEY_NAV_DOWN);break;
 case 'a':case 'A':feed_phone(VQEAF_KEY_NAV_LEFT);break;
 case 'd':case 'D':feed_phone(VQEAF_KEY_NAV_RIGHT);break;
 case 'e':case 'E':case '\r':case '\n':feed_phone(VQEAF_KEY_ENTER);break;
 case 'b':case 'B':case 0x7F:case 0x08:feed_phone(VQEAF_KEY_BACK);break;
 case 'q':case 'Q':feed_phone(VQEAF_KEY_QUICK);break;
 case 'l':case 'L':feed_phone(VQEAF_KEY_LOCK);break;
 case 'p':feed_button(VQEAF_KEY_POWER,VQEAF_KEY_TAP);break;
 case 'P':feed_button(VQEAF_KEY_POWER,VQEAF_KEY_LONG_PRESS);break;
 default:break;
 }
}
static void task_buttons(void *arg){
 (void)arg;
 vqeaf_pmenu_init(&menu);
 menu_print("PMENU: ready; hold Power to open","\r\n");
 menu_print("PHONE: W/A/S/D move, E select, B back, Q quick, L lock",0);
 for(;;){
  while(vqeaf_in8(0x3FD)&0x01){
   char c=(char)vqeaf_in8(0x3F8);
   note_user_activity();
   feed_serial(c);
  }
  pump_events();
  __asm__ volatile("hlt");
 }
}
static void task_fault_test(void *arg){
 (void)arg;
 task_print("T3"," entered",0);
 uint64_t guard=vqeaf_task_stack_base(vqeaf_sched_current())-VQEAF_PAGE_SIZE;
 task_print("T3"," guard page at 0x",guard);
 vqeaf_pf_expect(guard,3); /* movq (reg),reg is 3 bytes */
 uint64_t probe;
 __asm__ volatile("movq (%1),%0":"=r"(probe):"r"(guard));
 (void)probe;
 task_print("T3"," recovered from guard page fault; done",0);
 vqeaf_task_exit();
}
/* Native UI refresh task: repaints the phone shell four times a second and
 * samples the CMOS RTC once per second. */
static void serial_pad2(uint32_t v){
 vqeaf_serial_char((char)('0'+(v/10u)%10u));
 vqeaf_serial_char((char)('0'+v%10u));
}
static void rtc_log(const VqeafRtcTime *t){
 __asm__ volatile("cli");
 vqeaf_serial_write("RTC: CMOS ");
 serial_u64(t->year);vqeaf_serial_char('-');serial_pad2(t->month);
 vqeaf_serial_char('-');serial_pad2(t->day);
 vqeaf_serial_char(' ');serial_pad2(t->hour);
 vqeaf_serial_char(':');serial_pad2(t->minute);
 vqeaf_serial_char(':');serial_pad2(t->second);
 vqeaf_serial_write("\r\n");
 __asm__ volatile("sti");
}
static void task_ui(void *arg){
 (void)arg;uint64_t last=0,rtc_poll=0;
 int rtc_logged=0,rtc_failed=0;uint32_t rtc_sec=0xFFFFFFFFu;
 for(;;){
  uint64_t t=vqeaf_timer_ticks();
  vqeaf_power_tick(&power_manager,t*10u);
  if(t-rtc_poll>=100u){
   rtc_poll=t;
   VqeafRtcTime rt;
   if(vqeaf_rtc_read(&rt)==0){
    if(rt.second!=rtc_sec){
     rtc_sec=rt.second;
     vqeaf_ui_set_clock(1,rt.year,rt.month,rt.day,rt.hour,rt.minute,rt.second);
    }
    if(!rtc_logged){rtc_logged=1;rtc_log(&rt);}
   }else{
    vqeaf_ui_set_clock(0,0,0,0,0,0,0);
    if(!rtc_failed){rtc_failed=1;power_log("RTC: CMOS read failed\r\n");}
   }
  }
  if(vqeaf_ui_ready()&&t-last>=25u){last=t;__asm__ volatile("cli" ::: "memory");vqeaf_ui_render(t);__asm__ volatile("sti" ::: "memory");}
  __asm__ volatile("hlt");
 }
}
static void storage_log(const char *s){
 __asm__ volatile("cli");vqeaf_serial_write(s);vqeaf_serial_write("\r\n");__asm__ volatile("sti");
}
static void storage_num(const char *prefix,uint64_t v,const char *suffix){
 __asm__ volatile("cli");vqeaf_serial_write(prefix);serial_u64(v);
 vqeaf_serial_write(suffix);vqeaf_serial_write("\r\n");__asm__ volatile("sti");
}
/* SATA/AHCI phase: every AHCI disk is a block device. Mount the first volume
 * that carries a FAT16 partition at /storage, read a known file, exercise the
 * driver write path on the last sector (restored afterwards), prove the block
 * registration is read-only, then unmount and remount the same namespace. */
static void task_storage_sata(void){
 if(!vqeaf_ahci_ready()){
  storage_log("STORAGE: no AHCI disk; SATA storage stays unbound");
  return;
 }
 uint8_t sec[VQEAF_VBLK_SECTOR_SIZE];
 VqeafPartition parts[4];
 uint32_t disks=vqeaf_ahci_disk_count();
 storage_num("STORAGE: AHCI disks=",disks,"");
 int chosen=-1;uint64_t base=0;
 for(uint32_t i=0;i<disks;i++){
  uint32_t dev=vqeaf_ahci_dev_id(i);
  int r=vqeaf_block_read(dev,0,1,sec);
  if(r||sec[510]!=0x55u||sec[511]!=0xAAu){
   storage_num("STORAGE: AHCI disk has no MBR; skipped index=",i,"");
   continue;
  }
  int n=vqeaf_mbr_parse(dev,parts,4);
  if(n<1){storage_num("STORAGE: AHCI disk has no partition; skipped index=",i,"");continue;}
  r=vqeaf_storage_mount_fat16(dev,parts[0].start_lba,"/storage",0);
  if(r){storage_num("STORAGE: AHCI disk FAT16 mount failed r=",(uint64_t)(-r),"");continue;}
  chosen=(int)i;base=parts[0].start_lba;
  storage_num("STORAGE: AHCI disk mounted at /storage index=",i,"");
  break;
 }
 if(chosen<0){storage_log("STORAGE: no mountable AHCI volume");return;}
 uint32_t dev=vqeaf_ahci_dev_id((uint32_t)chosen);
 VqeafVnode *node=0;
 int r=vqeaf_storage_walk("/storage/AHCI.TXT",&node);
 if(r)vqeaf_panic("STORAGE: AHCI VFS lookup failed",(uint64_t)r);
 char content[96];
 int64_t got=vqeaf_vfs_read(node,0,content,sizeof content-1u);
 if(got<0)vqeaf_panic("STORAGE: AHCI VFS read failed",(uint64_t)got);
 content[got]=0;
 __asm__ volatile("cli");
 vqeaf_serial_write("STORAGE: /storage/AHCI.TXT -> ");vqeaf_serial_write(content);vqeaf_serial_write("\r\n");
 __asm__ volatile("sti");
 /* Driver write path on the last sector; the original bytes are restored. */
 uint64_t cap=vqeaf_ahci_capacity((uint32_t)chosen);
 uint8_t orig[VQEAF_VBLK_SECTOR_SIZE],pattern[VQEAF_VBLK_SECTOR_SIZE],back[VQEAF_VBLK_SECTOR_SIZE];
 r=vqeaf_ahci_read_sync((uint32_t)chosen,cap-1u,1,orig);
 if(r)vqeaf_panic("STORAGE: AHCI scratch read failed",(uint64_t)r);
 for(uint32_t i=0;i<sizeof pattern;i++)pattern[i]=(uint8_t)(0x5Au^(i&0xFFu));
 r=vqeaf_ahci_write_sync((uint32_t)chosen,cap-1u,1,pattern);
 if(r)vqeaf_panic("STORAGE: AHCI scratch write failed",(uint64_t)r);
 r=vqeaf_block_read(dev,cap-1u,1,back);
 if(r)vqeaf_panic("STORAGE: AHCI scratch readback failed",(uint64_t)r);
 for(uint32_t i=0;i<sizeof pattern;i++)
  if(pattern[i]!=back[i])vqeaf_panic("STORAGE: AHCI scratch mismatch",i);
 r=vqeaf_ahci_write_sync((uint32_t)chosen,cap-1u,1,orig);
 if(r)vqeaf_panic("STORAGE: AHCI scratch restore failed",(uint64_t)r);
 r=vqeaf_block_read(dev,cap-1u,1,back);
 if(r)vqeaf_panic("STORAGE: AHCI restore readback failed",(uint64_t)r);
 for(uint32_t i=0;i<sizeof orig;i++)
  if(orig[i]!=back[i])vqeaf_panic("STORAGE: AHCI restore mismatch",i);
 storage_log("STORAGE: AHCI scratch write + readback + restore verified");
 int e=vqeaf_block_write(dev,0,1,back);
 if(e!=-3)vqeaf_panic("STORAGE: AHCI read-only contract broken",(uint64_t)e);
 storage_log("STORAGE: AHCI block registration is read-only (write rejected)");
 /* Unmount must drop the namespace entry; remount must serve the same file. */
 r=vqeaf_storage_unmount("/storage");
 if(r)vqeaf_panic("STORAGE: AHCI unmount failed",(uint64_t)r);
 VqeafVnode *gone=0;
 r=vqeaf_storage_walk("/storage/AHCI.TXT",&gone);
 if(r!=-2)vqeaf_panic("STORAGE: AHCI unmount left a live path",(uint64_t)r);
 r=vqeaf_storage_mount_fat16(dev,base,"/storage",0);
 if(r)vqeaf_panic("STORAGE: AHCI remount failed",(uint64_t)r);
 node=0;r=vqeaf_storage_walk("/storage/AHCI.TXT",&node);
 if(r)vqeaf_panic("STORAGE: AHCI remount lookup failed",(uint64_t)r);
 got=vqeaf_vfs_read(node,0,content,sizeof content-1u);
 if(got<0)vqeaf_panic("STORAGE: AHCI remount read failed",(uint64_t)got);
 storage_log("STORAGE: AHCI unmount/remount verified");
 vqeaf_ui_set_storage_status(1,cap,"/storage","AHCI.TXT",content);
 vqeaf_cap_set("storage.ahci.mount",VQEAF_CAP_READY,(uint32_t)chosen,0);
 storage_log("STORAGE: AHCI phase complete");
}
/* Storage hardware stack: VirtIO controller -> block device -> MBR partition
 * -> read-only FAT16 mount -> VFS read, plus explicit I/O error paths. Runs as
 * a normal kernel task so async completions arrive through the INTx handler. */
static void task_storage(void *arg){
 (void)arg;
 if(!vqeaf_virtio_blk_ready()){
  storage_log("STORAGE: no VirtIO block device; virtio phase skipped");
  task_storage_sata();
  vqeaf_task_exit();return;
 }
 uint32_t dev=vqeaf_virtio_blk_dev_id();
 uint8_t sec[VQEAF_VBLK_SECTOR_SIZE];
 VqeafPartition parts[4];
 VqeafFat16Mount mnt;
 char content[96];
 int r;
 /* Two requests in flight at once proves the virtqueue is really driven. */
 r=vqeaf_virtio_blk_concurrent_self_test();
 if(r)vqeaf_panic("VIRTIO: concurrent async requests failed",(uint64_t)r);
 storage_log("VIRTIO: 2 in-flight async reads completed via INTx");
 /* A command the device cannot serve must come back as a device error. */
 r=vqeaf_virtio_blk_error_self_test();
 if(r)vqeaf_panic("VIRTIO: unsupported-command error path failed",(uint64_t)r);
 storage_log("VIRTIO: unsupported command returned device error");
 /* Exercise the driver's async write path on a scratch sector outside the
  * FAT16 partition; the block-layer registration stays read-only. */
 uint64_t scratch=vqeaf_virtio_blk_capacity()-1u;
 uint8_t pattern[VQEAF_VBLK_SECTOR_SIZE],readback[VQEAF_VBLK_SECTOR_SIZE];
 for(uint32_t i=0;i<sizeof pattern;i++)pattern[i]=(uint8_t)(0xA5u^(i&0xFFu));
 r=vqeaf_virtio_blk_write_sync(scratch,1,pattern);
 if(r)vqeaf_panic("VIRTIO: async write failed",(uint64_t)r);
 r=vqeaf_virtio_blk_read_sync(scratch,1,readback);
 if(r)vqeaf_panic("VIRTIO: readback failed",(uint64_t)r);
 for(uint32_t i=0;i<sizeof pattern;i++)
  if(pattern[i]!=readback[i])vqeaf_panic("VIRTIO: readback mismatch",i);
 storage_log("VIRTIO: async sector write + readback verified");
 /* Phase 2: discovery, partition parsing, read-only mount and VFS read. */
 r=vqeaf_block_read(dev,0,1,sec);
 if(r||sec[510]!=0x55u||sec[511]!=0xAAu)vqeaf_panic("STORAGE: MBR read failed",(uint64_t)r);
 int n=vqeaf_mbr_parse(dev,parts,4);
 if(n!=1)vqeaf_panic("STORAGE: MBR partition count unexpected",(uint64_t)n);
 storage_num("STORAGE: MBR partition type=",parts[0].type," found");
 storage_num("STORAGE: partition start LBA=",parts[0].start_lba," sectors");
 r=vqeaf_fat16_mount(dev,parts[0].start_lba,&mnt);
 if(r)vqeaf_panic("STORAGE: FAT16 mount failed",(uint64_t)r);
 vqeaf_cap_set("storage.fat16",VQEAF_CAP_READY,0,0);
 storage_log("STORAGE: FAT16 mounted read-only");
 VqeafVnode *node=0;
 r=vqeaf_fat16_walk(&mnt,"/VQEAF.TXT",&node);
 if(r)vqeaf_panic("STORAGE: VFS lookup failed",(uint64_t)r);
 int64_t got=vqeaf_vfs_read(node,0,content,sizeof content-1u);
 if(got<0)vqeaf_panic("STORAGE: VFS read failed",(uint64_t)got);
 content[got]=0;
 __asm__ volatile("cli");
 vqeaf_serial_write("STORAGE: /VQEAF.TXT -> ");vqeaf_serial_write(content);vqeaf_serial_write("\r\n");
 __asm__ volatile("sti");
 r=vqeaf_storage_mount_fat16(dev,parts[0].start_lba,"/data",0);
 if(r)vqeaf_panic("STORAGE: mount namespace rejected",(uint64_t)r);
 VqeafVnode *ns_root=0;const char *rel=0;
 r=vqeaf_storage_resolve("/data/VQEAF.TXT",&ns_root,&rel);
 if(r||!ns_root)vqeaf_panic("STORAGE: mount namespace resolve failed",(uint64_t)r);
 storage_log("STORAGE: /data mount namespace resolves VQEAF.TXT");
 vqeaf_ui_set_storage_status(1,vqeaf_virtio_blk_capacity(),"/data","VQEAF.TXT",content);
 /* Explicit I/O failure paths: out of bounds, zero length, read-only media. */
 uint8_t probe[VQEAF_VBLK_SECTOR_SIZE];
 int e1=vqeaf_block_read(dev,vqeaf_virtio_blk_capacity(),1,probe);
 int e2=vqeaf_block_read(dev,0,0,probe);
 int e3=vqeaf_block_write(dev,0,1,probe);
 if(e1!=-2||e2!=-1||e3!=-3)
  vqeaf_panic("STORAGE: I/O error contract broken",((uint64_t)e1<<32)|((uint64_t)e2<<16)|(uint64_t)e3);
 storage_log("STORAGE: I/O error tests passed (bounds / zero / read-only)");
 storage_num("STORAGE: INTx count=",vqeaf_virtio_blk_irq_count()," interrupts");
 storage_num("STORAGE: async completions=",vqeaf_virtio_blk_completion_count()," requests");
 storage_log("STORAGE: phase 1+2 complete");
 task_storage_sata();
 vqeaf_task_exit();
}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st){
 EFI_LOADED_IMAGE_PROTOCOL*li=0;VqeafBootInfoV3*info=0;
 UINTN bytes=0,key=0,ds=0;uint32_t ver=0;void*map=0;EFI_STATUS s;
 vqeaf_serial_init(); vqeaf_serial_write("VQEAF kernel 0.2: native entry reached\r\n");
 s=((EFI_HANDLE_PROTOCOL)BS(st)->HandleProtocol)(image,&loaded_guid,(void**)&li);
 if(EFI_ERROR(s)||!li||li->LoadOptionsSize<sizeof(VqeafBootInfoV3))goto invalid;
 info=(VqeafBootInfoV3*)li->LoadOptions;
 if(__builtin_memcmp(info->magic,VQEAF_BOOT_MAGIC,8)||info->version!=VQEAF_BOOT_PROTOCOL_VERSION||info->size<sizeof(*info)||info->architecture!=VQEAF_ARCH_X86_64)goto invalid;
 GET_MAP getmap=(GET_MAP)BS(st)->GetMemoryMap;
 ALLOC_POOL alloc=(ALLOC_POOL)BS(st)->AllocatePool;
 FREE_POOL freepool=(FREE_POOL)BS(st)->FreePool;
 EXIT_BOOT exitboot=(EXIT_BOOT)BS(st)->ExitBootServices;
 s=getmap(&bytes,0,&key,&ds,&ver);
 if(s!=EFI_BUFFER_TOO_SMALL||!bytes||!ds)goto invalid;
 bytes+=ds*8;
 s=alloc(EFI_LOADER_DATA,bytes,&map);
 if(EFI_ERROR(s)||!map)goto invalid;
 for(unsigned attempt=0;attempt<5;attempt++){
  UINTN capacity=bytes;
  s=getmap(&capacity,map,&key,&ds,&ver);
  if(s==EFI_BUFFER_TOO_SMALL){
   freepool(map);map=0;bytes=capacity+ds*8;
   s=alloc(EFI_LOADER_DATA,bytes,&map);
   if(EFI_ERROR(s)||!map)goto invalid;
   continue;
  }
  if(EFI_ERROR(s))goto invalid;
  info->memory_map=map;info->memory_map_size=capacity;
  info->memory_descriptor_size=ds;info->memory_descriptor_version=ver;
  s=exitboot(image,key);
  if(!EFI_ERROR(s))break;
  if(s!=2u)goto invalid;
 }
 if(EFI_ERROR(s)){vqeaf_serial_write("ExitBootServices retry limit reached\r\n");stop();}
 vqeaf_serial_write("VQEAF kernel: ExitBootServices SUCCESS\r\n");
 /* No UEFI Boot Services calls are permitted beyond this point. */
 vqeaf_arch_init();
 if(vqeaf_pmm_init(info->memory_map,info->memory_map_size,info->memory_descriptor_size)!=0)
  vqeaf_panic("Physical memory map invalid",info->memory_map_size);
 vqeaf_serial_write("Memory: physical page allocator online; free pages=");
 serial_u64(vqeaf_pmm_free_count());vqeaf_serial_write("\r\n");
 /* Identity map every memory-map range below 4 GB at 4 KiB granularity so
  * individual pages (guard pages) can later be unmapped. */
 uint64_t root=vqeaf_vm_create_root();
 if(!root)vqeaf_panic("VM: root allocation failed",0);
 typedef struct { uint32_t type,pad; uint64_t physical_start,virtual_start,number_of_pages,attribute; } Desc;
 uint64_t mapped=0;
 for(uint64_t off=0;off+info->memory_descriptor_size<=info->memory_map_size;off+=info->memory_descriptor_size){
  const Desc *d=(const Desc*)((const uint8_t*)info->memory_map+off);
  if(!d->number_of_pages)continue;
  uint64_t start=d->physical_start,end=start+d->number_of_pages*VQEAF_PAGE_SIZE;
  if(end>VQEAF_PHYS_LIMIT)end=VQEAF_PHYS_LIMIT;
  if(start>=end)continue;
  if(vqeaf_vm_map_range(root,start,start,(end-start)/VQEAF_PAGE_SIZE,VQEAF_VM_WRITE)!=0)
   vqeaf_panic("VM: identity mapping failed",start);
  mapped+=(end-start)/VQEAF_PAGE_SIZE;
 }
 vqeaf_vm_activate(root);
 vqeaf_serial_write("VM: identity page tables active; CR3=0x");serial_u64(vqeaf_vm_current_root());
 vqeaf_serial_write(" pages=");serial_u64(mapped);vqeaf_serial_write("\r\n");
 /* Keep the GOP linear framebuffer mapped after firmware handoff. It is MMIO
  * below 4 GB and is normally already in the identity map; add write + PCD
  * on the leaf entries without replacing existing mappings. */
 int fb_usable=0;
 if(info->framebuffer_size&&info->framebuffer_base){
  uint64_t fb_start=info->framebuffer_base&~(VQEAF_PAGE_SIZE-1);
  uint64_t fb_end=(info->framebuffer_base+info->framebuffer_size+VQEAF_PAGE_SIZE-1)&~(VQEAF_PAGE_SIZE-1);
  if(fb_end>fb_start&&fb_end<=VQEAF_PHYS_LIMIT){
   uint64_t pages=(fb_end-fb_start)/VQEAF_PAGE_SIZE;
   if(vqeaf_vm_map_range_or_update(root,fb_start,fb_start,pages,VQEAF_VM_WRITE|VQEAF_VM_CACHE_DISABLE)==0){
    fb_usable=1;
    vqeaf_serial_write("UI: GOP framebuffer mapped, pages=");serial_u64(pages);vqeaf_serial_write("\r\n");
   }else vqeaf_serial_write("UI: framebuffer mapping failed; serial-only\r\n");
  }else vqeaf_serial_write("UI: framebuffer outside 4 GB identity map; serial-only\r\n");
 }else vqeaf_serial_write("UI: no GOP framebuffer in handoff; serial-only\r\n");
 /* Kernel heap on contiguous identity-mapped pages. */
 #define VQEAF_HEAP_PAGES 256u
 uint64_t heap_phys=vqeaf_pmm_alloc_pages(VQEAF_HEAP_PAGES);
 if(!heap_phys)vqeaf_panic("kheap: arena allocation failed",0);
 if(vqeaf_kheap_init((void*)heap_phys,VQEAF_HEAP_PAGES*VQEAF_PAGE_SIZE)!=0)vqeaf_panic("kheap: init failed",0);
 void *probe=vqeaf_kmalloc(4096);if(!probe)vqeaf_panic("kheap: kmalloc probe failed",0);
 vqeaf_kfree(probe);
 VqeafKHeapStats hs;if(vqeaf_kheap_stats(&hs)!=0)vqeaf_panic("kheap: stats failed",0);
 init_core_services(fb_usable);
 {int vr=vqeaf_virtio_blk_init();
  if(vr==0)vqeaf_serial_write("VIRTIO: controller online\r\n");
  else{vqeaf_serial_write("VIRTIO: controller unavailable; storage stack stays unbound r=");serial_u64((uint64_t)(-vr));vqeaf_serial_write("\r\n");}}
 {int nr=vqeaf_virtio_net_init();
 if(nr!=0)vqeaf_serial_write("NET: virtio-net unavailable; networking stays unbound\r\n");}
 {int ar=vqeaf_ahci_init();
  if(ar==0)vqeaf_serial_write("AHCI: controller online\r\n");
  else{vqeaf_serial_write("AHCI: controller unavailable; SATA phase will skip r=");serial_u64((uint64_t)(-ar));vqeaf_serial_write("\r\n");}}
 vqeaf_serial_write("kheap: online, arena=");serial_u64(hs.arena_size);
 vqeaf_serial_write(" free=");serial_u64(hs.free_bytes);vqeaf_serial_write("\r\n");
 vqeaf_arch_ring3_init();
 vqeaf_sched_init();
 vqeaf_sched_set_kernel_root(root);
 /* Real preemptive tasks, each with its own kernel stack and guard page. */
 if(vqeaf_task_create(task_counter,(void*)1)<0)vqeaf_panic("SCHED: T1 creation failed",0);
 if(vqeaf_task_create(task_counter,(void*)2)<0)vqeaf_panic("SCHED: T2 creation failed",0);
 if(vqeaf_task_create(task_fault_test,0)<0)vqeaf_panic("SCHED: T3 creation failed",0);
 if(vqeaf_task_create(task_buttons,0)<0)vqeaf_panic("SCHED: T4 creation failed",0);
 /* Ring-3 task from the embedded ELF user program. */
 {
  uint64_t uroot=vqeaf_vm_user_root(root);if(!uroot)vqeaf_panic("ELF: user root alloc failed",0);
  uint64_t entry=0;
  int elf_r=vqeaf_elf_load(uroot,vqeaf_user_hello_elf,VQEAF_USER_HELLO_SIZE,&entry);
  if(elf_r!=0)
   vqeaf_panic("ELF: user program load failed",((uint64_t)(-elf_r)<<48)|entry);
  int uslot=vqeaf_task_create_user(uroot,entry,VQEAF_USER_STACK_BASE+VQEAF_USER_STACK_PAGES*VQEAF_PAGE_SIZE);
  if(uslot<0)vqeaf_panic("SCHED: user task creation failed",entry);
  vqeaf_serial_write("ELF: user program loaded; entry=0x");serial_u64(entry);vqeaf_serial_write("\r\n");
 }
 if(vqeaf_task_create(task_ui,0)<0)vqeaf_panic("SCHED: UI creation failed",0);
 if(vqeaf_task_create(task_storage,0)<0)vqeaf_panic("SCHED: storage creation failed",0);
 if(fb_usable){
  /* The GOP framebuffer is a usable software display; no backlight driver is claimed. */
  if(vqeaf_ui_init(info->framebuffer_base,info->framebuffer_width,info->framebuffer_height,
                   info->framebuffer_stride,info->framebuffer_format,
                   vqeaf_pmm_free_count(),7)==0){
   vqeaf_ui_set_power_status((uint32_t)power_manager.timeout,0,0,0,0);
   vqeaf_serial_write("UI: native shell online\r\n");
  }
  else vqeaf_serial_write("UI: framebuffer rejected; serial-only\r\n");
 }
 /* Unmap each task's guard page now that paging is active. */
 for(unsigned i=0;i<VQEAF_TASK_CAPACITY;i++){
  uint64_t stack=vqeaf_task_stack_base((int)i);
  if(stack){
   if(vqeaf_vm_unmap(root,stack-VQEAF_PAGE_SIZE)!=0)
    vqeaf_panic("VM: guard page unmap failed",stack);
  }
 }
 vqeaf_serial_write("SCHED: tasks created with guard pages\r\n");
 vqeaf_arch_timer_init(100);
 vqeaf_sched_start();
 vqeaf_panic("Scheduler returned",0);
invalid:
 st->ConOut->OutputString(st->ConOut,bad);
 vqeaf_serial_write("VQEAF kernel: handoff or memory-map failure\r\n");
 return EFI_LOAD_ERROR;
}
