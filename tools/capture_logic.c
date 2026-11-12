#include <libsigrok/libsigrok.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *file;
static void check(int r,const char *s) { if(r!=SR_OK) {fprintf(stderr,"%s: %s\n",s,sr_strerror(r)); exit(1);} }
static void feed(const struct sr_dev_inst *d,const struct sr_datafeed_packet *p,void *arg) {
 (void)d;(void)arg;
 if(p->type==SR_DF_LOGIC) {const struct sr_datafeed_logic *l=p->payload; if(l->unitsize!=1) exit(2); if(fwrite(l->data,1,l->length,file)!=l->length) exit(6);}
}
int main(int argc,char **argv) {
 if(argc!=4) {fprintf(stderr,"Usage: %s output.bin samples samplerate_hz\n",argv[0]); return 2;}
 struct sr_context *ctx; check(sr_init(&ctx),"init");
 struct sr_dev_driver **drivers=sr_driver_list(ctx),*driver=NULL;
 for(unsigned i=0;drivers[i];++i) if(!strcmp(drivers[i]->name,"fx2lafw")) driver=drivers[i];
 if(!driver) return 3;
 check(sr_driver_init(ctx,driver),"driver init");
 GSList *devices=sr_driver_scan(driver,NULL);
 if(!devices) {fprintf(stderr,"No analyzer found\n");return 4;}
 struct sr_dev_inst *dev=devices->data;
 check(sr_dev_open(dev),"open analyzer (PulseView may own device)");
 check(sr_config_set(dev,NULL,SR_CONF_SAMPLERATE,g_variant_new_uint64(strtoull(argv[3],NULL,10))),"samplerate");
 check(sr_config_set(dev,NULL,SR_CONF_LIMIT_SAMPLES,g_variant_new_uint64(strtoull(argv[2],NULL,10))),"limit");
 file=fopen(argv[1],"wb"); if(!file) return 5;
 struct sr_session *session; check(sr_session_new(ctx,&session),"session");
 check(sr_session_dev_add(session,dev),"add");
 check(sr_session_datafeed_callback_add(session,feed,NULL),"callback");
 check(sr_session_start(session),"start");
 fprintf(stderr,"CAPTURE_STARTED\n");fflush(stderr);
 check(sr_session_run(session),"run");
 fclose(file); sr_session_destroy(session);sr_dev_close(dev);sr_exit(ctx);return 0;
}
