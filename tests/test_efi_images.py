import struct, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read_fat12(path):
    data=path.read_bytes()
    bps,spc,reserved,fats,roots,total,media,spf=struct.unpack_from("<HBHBHHBH",data,11)
    assert bps==512 and spc==1 and reserved==1 and fats==2 and media==0xF0
    root_sectors=(roots*32+bps-1)//bps
    data_start=reserved+fats*spf+root_sectors
    fat=data[reserved*bps:(reserved+spf)*bps]
    def next_cluster(c):
        off=c+c//2; value=fat[off]|(fat[off+1]<<8)
        return (value>>4)&0xFFF if c&1 else value&0xFFF
    def cluster_data(c,size=None):
        chunks=[]
        while c>=2 and c<0xFF8:
            off=(data_start+c-2)*bps
            chunks.append(data[off:off+bps])
            c=next_cluster(c)
        blob=b"".join(chunks)
        return blob if size is None else blob[:size]
    def parse_dir(blob):
        result={}
        for i in range(0,len(blob)-31,32):
            e=blob[i:i+32]
            if e[0]==0: break
            if e[0]==0xE5 or e[11]==0x0F: continue
            name=e[:8].decode("ascii").rstrip()
            ext=e[8:11].decode("ascii").rstrip()
            full=name+("."+ext if ext else "")
            cluster=struct.unpack_from("<H",e,26)[0]
            size=struct.unpack_from("<I",e,28)[0]
            result[full]=(e[11],cluster,size)
        return result
    root_off=(reserved+fats*spf)*bps
    root=parse_dir(data[root_off:root_off+root_sectors*bps])
    def walk(prefix, entries):
        out={}
        for name,(attr,cluster,size) in entries.items():
            if name in (".",".."): continue
            path=(prefix+"/"+name).strip("/")
            if attr&0x10: out.update(walk(path,parse_dir(cluster_data(cluster))))
            else: out[path]=cluster_data(cluster,size)
        return out
    return walk("",root),data
class EFIImageTests(unittest.TestCase):
    def test_normal_image_contains_boot_manager_and_kernel(self):
        files,raw=read_fat12(ROOT/"dist"/"vqeaf-x86_64-uefi.img")
        self.assertIn("EFI/BOOT/BOOTX64.EFI",files)
        self.assertIn("EFI/VQEAF/KERNEL.EFI",files)
        self.assertIn("EFI/VQEAF/RECOVERY.EFI",files)
        self.assertIn(b"BOOTX64.EFI",files["STARTUP.NSH"])
    def test_recovery_image_boots_recovery_entry(self):
        files,raw=read_fat12(ROOT/"dist"/"vqeaf-x86_64-recovery.img")
        self.assertIn(b"RECOVERY.EFI",files["STARTUP.NSH"])
        self.assertEqual(files["EFI/BOOT/BOOTX64.EFI"],files["EFI/VQEAF/RECOVERY.EFI"])
    def test_efi_binary_headers_are_x64_efi_applications(self):
        for name in ("BOOTX64.EFI","KERNEL.EFI","RECOVERY.EFI"):
            blob=(ROOT/"build"/name).read_bytes()
            pe=struct.unpack_from("<I",blob,0x3C)[0]
            machine=struct.unpack_from("<H",blob,pe+4)[0]
            magic=struct.unpack_from("<H",blob,pe+24)[0]
            subsystem=struct.unpack_from("<H",blob,pe+24+68)[0]
            self.assertEqual((machine,magic,subsystem),(0x8664,0x20B,10))
if __name__=="__main__": unittest.main()
