/* SDK boundary tests: no simulator, OpenGL context or aircraft files touched. */
#define A380X_TRIM_TEST 1
#include "plugin.c"
#include <assert.h>

typedef struct { float f,values[48]; int i; } Raw;
typedef struct { char name[160]; XPLMGetDataf_f read; XPLMSetDataf_f write; void *context; int active; } Output;
static Raw raw[RAW_COUNT];
static Output outputs[OUT_COUNT];
static char model[2048]="/fake/A380X-XP12/A380X-XP12.acf";
static int device_alive,registrations,handlers,callbacks,writes,device_failure;
static XPLMFlightLoop_f loop;

XPLMDataRef XPLMFindDataRef(const char *n)
{
    int i;
    for(i=0;i<RAW_COUNT;i++)if(!strcmp(n,raw_names[i]))return &raw[i];
    for(i=0;i<OUT_COUNT;i++)if(outputs[i].active && !strcmp(n,outputs[i].name))return &outputs[i];
    return NULL;
}
int XPLMCanWriteDataRef(XPLMDataRef ref) { return ref==&raw[PITCH]||ref==&raw[RUDDER]; }
float XPLMGetDataf(XPLMDataRef ref) { return ((Raw *)ref)->f; }
int XPLMGetDatai(XPLMDataRef ref) { return ((Raw *)ref)->i; }
void XPLMSetDataf(XPLMDataRef ref,float v)
{
    assert(ref==&raw[PITCH]||ref==&raw[RUDDER]);assert(isfinite(v));
    ((Raw *)ref)->f=v;writes++;
}
int XPLMGetDatavf(XPLMDataRef ref,float *v,int offset,int count)
{
    memcpy(v,((Raw *)ref)->values+offset,(size_t)count*sizeof(float));return count;
}
XPLMDataRef XPLMRegisterDataAccessor(const char *name,XPLMDataTypeID type,int writable,
    XPLMGetDatai_f ri,XPLMSetDatai_f wi,XPLMGetDataf_f rf,XPLMSetDataf_f wf,
    XPLMGetDatad_f rd,XPLMSetDatad_f wd,XPLMGetDatavi_f ria,XPLMSetDatavi_f wia,
    XPLMGetDatavf_f rfa,XPLMSetDatavf_f wfa,XPLMGetDatab_f rb,XPLMSetDatab_f wb,
    void *read_context,void *write_context)
{
    Output *o=&outputs[registrations++];
    (void)type;(void)writable;(void)ri;(void)wi;(void)rd;(void)wd;(void)ria;(void)wia;
    (void)rfa;(void)wfa;(void)rb;(void)wb;(void)write_context;
    strcpy(o->name,name);o->read=rf;o->write=wf;o->context=read_context;o->active=1;return o;
}
void XPLMUnregisterDataAccessor(XPLMDataRef ref) { ((Output *)ref)->active=0; }
XPLMCommandRef XPLMCreateCommand(const char *name,const char *description)
{ (void)description;return (XPLMCommandRef)name; }
XPLMCommandRef XPLMFindCommand(const char *name) { return (XPLMCommandRef)name; }
void XPLMRegisterCommandHandler(XPLMCommandRef r,XPLMCommandCallback_f f,int before,void *context)
{ (void)r;(void)f;(void)context;assert(before==1);handlers++; }
void XPLMUnregisterCommandHandler(XPLMCommandRef r,XPLMCommandCallback_f f,int before,void *context)
{ (void)r;(void)f;(void)context;assert(before==1);handlers--; }
void XPLMRegisterFlightLoopCallback(XPLMFlightLoop_f f,float interval,void *context)
{ (void)interval;(void)context;loop=f;callbacks++; }
void XPLMUnregisterFlightLoopCallback(XPLMFlightLoop_f f,void *context)
{ (void)context;assert(loop==f);callbacks--;loop=NULL; }
void XPLMGetNthAircraftModel(int index,char *name,char *path)
{ assert(index==0);strcpy(name,strrchr(model,'/')+1);strcpy(path,model); }
XPLMPluginID XPLMGetMyID(void) { return 42; }
void XPLMGetPluginInfo(XPLMPluginID id,char *name,char *path,char *signature,char *description)
{
    (void)id;(void)name;(void)signature;(void)description;
    strcpy(path,"/fake/A380X-XP12/plugins/A380XTrim/mac_x64/A380XTrim.xpl");
}
void XPLMEnableFeature(const char *name,int enable) { (void)name;(void)enable; }
void XPLMDebugString(const char *message) { assert(message); }
XPLMAvionicsID XPLMCreateAvionicsEx(XPLMCreateAvionics_t *p)
{
    assert(p->structSize==(int)sizeof(*p) && p->drawCallback);
    assert(!strcmp(p->deviceID,"a380x.xp12.trim.rudder"));
    if(device_failure)return NULL;
    assert(!device_alive);device_alive=1;return (XPLMAvionicsID)(uintptr_t)7;
}
void XPLMDestroyAvionics(XPLMAvionicsID id) { assert(id && device_alive);device_alive=0; }
void XPLMAvionicsNeedsDrawing(XPLMAvionicsID id) { assert(id && device_alive); }
int XPLMIsAvionicsBound(XPLMAvionicsID id) { return id && device_alive; }

static void send(int index,int phase)
{ assert(command_handler(commands[index].ref,(XPLMCommandPhase)phase,&commands[index])==0); }
int main(void)
{
    char name[256],signature[256],description[256];int i;
    raw[GROUND].i=raw[ELECTRIC].i=1;raw[BUS].values[0]=28;
    raw[UP_LIMIT].f=raw[DN_LIMIT].f=10;raw[PITCH_TIME].f=25;
    raw[YAW_LEFT].f=raw[YAW_RIGHT].f=30;raw[YAW_MIN].f=raw[YAW_MAX].f=.3f;
    raw[NZ].f=1;raw[IAS].f=200;raw[AGL].f=1000;
    raw[TIME_STEP].f=.02f;
    assert(XPluginStart(name,signature,description));
    assert(registrations==OUT_COUNT && handlers==COUNT(commands) && callbacks==1);
    assert(outputs[OUT_PITCH_SWITCH].read(outputs[OUT_PITCH_SWITCH].context)==.5f);
    assert(XPluginEnable() && XPluginEnable());
    send(0,0);send(0,2);loop(.02f,0,1,NULL);assert(raw[PITCH].f>0);
    send(1,0);loop(.02f,0,2,NULL);send(1,2);assert(fabs(raw[PITCH].f)<1e-8);
    send(3,0);for(i=0;i<50;i++)loop(.02f,0,3,NULL);send(3,2);
    assert(raw[RUDDER].f>0 && get_output((void *)OUT_DEGREES)>.9f);
    send(4,0);send(4,2);for(i=0;i<70;i++)loop(.02f,0,4,NULL);
    assert(raw[RUDDER].f==0);
    raw[GROUND].i=0;float old=raw[PITCH].f;send(0,0);loop(.02f,0,5,NULL);send(0,2);
    assert(raw[PITCH].f==old);
    set_law(NULL,TRIM_DIRECT);send(0,0);loop(.02f,0,6,NULL);send(0,2);assert(raw[PITCH].f>old);
    set_law(NULL,NAN);assert(plugin.law==TRIM_DIRECT);set_law(NULL,3);assert(plugin.law==TRIM_DIRECT);
    raw[PAUSED].i=1;old=raw[PITCH].f;send(0,0);loop(.02f,0,7,NULL);assert(raw[PITCH].f==old);
    raw[PAUSED].i=0;loop(.02f,0,8,NULL);assert(raw[PITCH].f==old);
    strcpy(model,"/fake/OtherPlane/OtherPlane.acf");int previous=writes;
    assert(command_handler(commands[0].ref,xplm_CommandBegin,&commands[0])==1);
    loop(.02f,0,9,NULL);assert(writes==previous && !get_output((void *)OUT_DEVICE));
    XPluginDisable();assert(!device_alive);assert(XPluginEnable());
    XPluginStop();assert(!device_alive && !handlers && !callbacks);
    for(i=0;i<OUT_COUNT;i++)assert(!outputs[i].active);
    registrations=0;assert(XPluginStart(name,signature,description));
    device_failure=1;assert(!XPluginEnable());XPluginStop();assert(!handlers && !callbacks);
    puts("PASS: SDK registration, cockpit/keyboard clicks, live native state, aircraft isolation, pause and lifecycle cleanup");
    return 0;
}
