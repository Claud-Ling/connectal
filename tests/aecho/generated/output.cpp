

/* Global Variable Definitions and Initialization */
class l_class_OC_EchoTest echoTest;
unsigned int stop_main_program;
class l_class_OC_Module *_ZN6Module5firstE;


//******************** vtables for Classes *******************
unsigned char *_ZTVN8EchoTest5driveE[4] = { (unsigned char *)_ZN8EchoTest5drive3RDYEv, (unsigned char *)_ZN8EchoTest5drive3ENAEv };
unsigned char *_ZTV4Rule[4] = {0 };
unsigned char *_ZTVN4Echo7respond8respond2E[4] = { (unsigned char *)_ZN4Echo7respond8respond23RDYEv, (unsigned char *)_ZN4Echo7respond8respond23ENAEv };
unsigned char *_ZTVN4Echo7respond8respond1E[4] = { (unsigned char *)_ZN4Echo7respond8respond13RDYEv, (unsigned char *)_ZN4Echo7respond8respond13ENAEv };
unsigned char *_ZTV5Fifo1IiE[12] = {0 };
unsigned char *_ZTV4FifoIiE[12] = {0 };
typedef unsigned char **RuleVTab;//Rules:
const RuleVTab ruleList[] = {
    _ZTVN8EchoTest5driveE, _ZTVN4Echo7respond8respond2E, _ZTVN4Echo7respond8respond1E, NULL};
//processing _ZN4Echo7respond8respond23ENAEv
void _ZN4Echo7respond8respond23ENAEv(class l_class_OC_Echo_KD__KD_respond_KD__KD_respond2 *Vthis) {
}
//processing _ZN4Echo7respond8respond23RDYEv
bool _ZN4Echo7respond8respond23RDYEv(class l_class_OC_Echo_KD__KD_respond_KD__KD_respond2 *Vthis) {
        return 1;
}
//processing _ZN4Echo7respond8respond13ENAEv
void _ZN4Echo7respond8respond13ENAEv(class l_class_OC_Echo_KD__KD_respond_KD__KD_respond1 *Vthis) {
        echoTest_ZZ_EchoTest_ZZ_echo_ZZ__ZZ_Echo_ZZ_fifo_ZZ__ZZ_Fifo1_int_.deq();
    unsigned int Vcall =     echoTest_ZZ_EchoTest_ZZ_echo_ZZ__ZZ_Echo_ZZ_fifo_ZZ__ZZ_Fifo1_int_.first();
        _ZN14EchoIndication4echoEi(Vcall);
}
//processing _ZN4Echo7respond8respond13RDYEv
bool _ZN4Echo7respond8respond13RDYEv(class l_class_OC_Echo_KD__KD_respond_KD__KD_respond1 *Vthis) {
    bool Vtmp__1 =     echoTest_ZZ_EchoTest_ZZ_echo_ZZ__ZZ_Echo_ZZ_fifo_ZZ__ZZ_Fifo1_int_.deq__RDY();
    bool Vtmp__2 =     echoTest_ZZ_EchoTest_ZZ_echo_ZZ__ZZ_Echo_ZZ_fifo_ZZ__ZZ_Fifo1_int_.first__RDY();
        return (Vtmp__1 & Vtmp__2);
}
//processing _ZN8EchoTest5drive3ENAEv
void _ZN8EchoTest5drive3ENAEv(class l_class_OC_EchoTest_KD__KD_drive *Vthis) {
        echoTest_ZZ_EchoTest_ZZ_echo_ZZ__ZZ_Echo_ZZ_fifo_ZZ__ZZ_Fifo1_int_.enq(22);
}
//processing _ZN8EchoTest5drive3RDYEv
bool _ZN8EchoTest5drive3RDYEv(class l_class_OC_EchoTest_KD__KD_drive *Vthis) {
    bool Vtmp__1 =     echoTest_ZZ_EchoTest_ZZ_echo_ZZ__ZZ_Echo_ZZ_fifo_ZZ__ZZ_Fifo1_int_.enq__RDY();
        return Vtmp__1;
}
//processing _ZN14EchoIndication4echoEi
void _ZN14EchoIndication4echoEi(unsigned int Vv) {
        printf((("Heard an echo: %d\n")), Vv);
        stop_main_program = 1;
}
//processing printf