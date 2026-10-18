#include "pxa.h"
#include "pxa_clock.h"
#include <stdarg.h>

/* Bounded decimal-only output keeps this probe freestanding. */
static int format(char *out,size_t size,const char *fmt,...) {
    size_t n=0; va_list args;va_start(args,fmt);
    for(;*fmt;++fmt) {
        if(*fmt=='%' && fmt[1]=='u') {
            char digits[10];unsigned count=0,v=va_arg(args,unsigned);
            do{digits[count++]=(char)('0'+v%10);v/=10;}while(v);
            while(count){char c=digits[--count];if(n+1<size)out[n++]=c;}
            ++fmt;
        }else if(n+1<size)out[n++]=*fmt;
    }
    if(size)out[n]=0;va_end(args);return (int)n;
}

static uint64_t token=1, previous;
static uint32_t periods[]={1000,8000,33000};
static uint32_t deltas[128], count, stage;
static int measuring, finished;
static void log_line(const char *s) { (void)pxa_log_write(PXA_LOG_LEVEL_INFO,s); }
static int fail(const char *s) {log_line(s);finished=1;return PXA_STATUS_INTERNAL;}
static int configure(uint32_t us) {++token;return pxa_clock_configure(token,us);}
int32_t pxa_app_start(const uint8_t *config,uint32_t length) {
    (void)config;(void)length;return pxa_clock_limits(token);
}
int32_t pxa_app_on_event(const uint8_t *bytes,uint32_t length) {
    pxa_event_t event;uint64_t now;char line[240];
    if(!pxa_parse_event(bytes,length,&event))return PXA_EVENT_UNHANDLED;
    if(finished)return PXA_EVENT_HANDLED;
    if(event.service==4 && event.opcode==PXA_CLOCK_LIMITS && event.token==token){
        pxa_clock_limits_result_t r;
        if(!pxa_clock_parse_limits(&event,token,&r)||r.status)return fail("CLOCK FAIL limits");
        format(line,sizeof(line),"CLOCK LIMITS min=%u max=%u step=%u quantum=%u flags=%u",
            r.limits.min_period_us,r.limits.max_period_us,r.limits.period_step_us,
            r.limits.dispatch_quantum_us,r.limits.flags);log_line(line);
        for(unsigned i=0;i<3;++i)if(!pxa_clock_period_supported(&r.limits,periods[i]))return fail("CLOCK FAIL unsupported test period");
        if(configure(periods[0]))return fail("CLOCK FAIL configure send");
    }else if(event.service==4 && event.opcode==PXA_CLOCK_CONFIGURE && event.token==token){
        pxa_clock_configure_result_t r;
        uint32_t expected=stage<3?periods[stage]:stage==3?999:stage==4?60000000:0;
        if(!pxa_clock_parse_configure(&event,token,expected,&r))return fail("CLOCK FAIL completion");
        if(stage==3){
            if(r.status!=PXA_STATUS_INVALID_ARGUMENT)return fail("CLOCK FAIL invalid period accepted");
            log_line("CLOCK invalid=999 rejected=1");
            measuring=1;count=0;previous=0;
        }else if(r.status)return fail("CLOCK FAIL status");
        else if(stage==4){log_line("CLOCK long=60000000 accepted=1");stage=5;if(configure(0))return fail("CLOCK FAIL cancel send");}
        else if(stage==5){finished=1;log_line("CLOCK TEST PASS cancel=1");}
        else {measuring=1;count=0;previous=0;}
    }else if(pxa_clock_parse_tick(&event,&now) && measuring){
        if(previous && now>previous)deltas[count++]=(uint32_t)(now-previous);
        previous=now;
        if(count==128){
            measuring=0;
            uint32_t sorted[128],late=0,current=stage<3?periods[stage]:33000;
            uint64_t sum=0;
            for(unsigned i=0;i<count;++i){
                uint32_t value=deltas[i];sum+=value;if(value>current*2u)++late;
                unsigned j=i;while(j && sorted[j-1]>value){sorted[j]=sorted[j-1];--j;}sorted[j]=value;
            }
            format(line,sizeof(line),"CLOCK RESULT stage=%u period=%u count=%u min=%u median=%u mean=%u max=%u late=%u",
                stage,current,count,sorted[0],(unsigned)(((uint64_t)sorted[63]+sorted[64])/2),
                (unsigned)(sum/count),sorted[127],late);log_line(line);
            ++stage;if(configure(stage<3?periods[stage]:stage==3?999:60000000))return fail("CLOCK FAIL next send");
        }
    }
    return PXA_EVENT_HANDLED;
}
void pxa_app_stop(uint32_t reason){(void)reason;}
