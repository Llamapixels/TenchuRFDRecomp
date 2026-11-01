from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/scripts'))
from seed_task_callbacks import discover
source='''X86_FUNC(__imp__sub_00100000) {
// 00100000 push 0x123450
// 00100005 call 0x002D4140
// 0010000A push 0x234560
// 0010000F call 0x00111111
// 00100014 mov eax, 0x345670
// 00100019 call 0x002D4140
// 0010001E push 0x0
// 00100020 call 0x002D4140
}
'''
assert discover(source)==[{'function':'00100000','callsite':'00100005','target':0x123450}]
print('PASS: task callback provenance; unrelated calls, non-push values and null callbacks excluded')
