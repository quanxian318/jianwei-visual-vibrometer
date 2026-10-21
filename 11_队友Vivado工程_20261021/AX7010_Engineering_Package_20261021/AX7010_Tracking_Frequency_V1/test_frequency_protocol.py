import struct,unittest
import numpy as np
from read_frequency import parse_frequency

class ProtocolTests(unittest.TestCase):
    def packet(self,baseline=2,valid=15):
        data=bytearray(8192);h=[0]*64
        h[0]=0x46524631;h[2]=0x50415353;h[4]=512;h[5]=valid
        h[12]=333333343;h[13]=256;h[14]=baseline
        struct.pack_into('<64I',data,0,*h)
        fields=np.frombuffer(data,dtype='<f4',offset=256,count=24).reshape(6,4)
        fields[0]=45;fields[1]=.1;fields[5]=3
        return data
    def test_no_candidate_without_baseline(self):
        s=parse_frequency(self.packet(baseline=0))
        self.assertTrue(all(v['candidate_hz'] is None for v in s['channels'].values()))
    def test_no_candidate_for_lost(self):
        s=parse_frequency(self.packet(valid=0))
        self.assertTrue(all(v['candidate_hz'] is None for v in s['channels'].values()))
    def test_ready_valid_candidate(self):
        s=parse_frequency(self.packet())
        self.assertTrue(all(v['candidate_hz']==3 for v in s['channels'].values()))
    def test_reject_odd_sequence(self):
        data=self.packet();struct.pack_into('<I',data,12,3)
        with self.assertRaises(ValueError):parse_frequency(data)
    def test_reject_nonfinite(self):
        data=self.packet();struct.pack_into('<f',data,300,float('nan'))
        with self.assertRaises(ValueError):parse_frequency(data)
    def test_reject_truncated(self):
        with self.assertRaises(ValueError):parse_frequency(self.packet()[:-1])
if __name__=='__main__':unittest.main()
