#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "vvvf/config.h"
#include "vvvf/control.h"
#include "vvvf/output.h"
/* Nonblocking USB console: f HZ=set immediately, r HZ=ramp, stop=all low.
   profiles lists presets; profile NAME selects one while stopped.
   These bounded commands allow repeatable logic-analyzer measurements. */
static void console(float *hz, float *target, bool *remote, vvvf_profile_t *profile, bool *braking, float *amplitude_override) {
    static char line[48];
    static unsigned length;
    static bool overflow;
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            line[length] = 0;
            if (!overflow && !strcmp(line,"stop")) {
                *hz = *target = 0; *remote = true; *amplitude_override=-1;
            } else if (!overflow && !strcmp(line,"m auto")) {
                *amplitude_override=-1;
            } else if (!overflow && length>2 && line[0]=='m' && line[1]==' ') {
                char *end;
                float value=strtof(line+2,&end);
                if(end!=line+2 && *end==0 && isfinite(value) && value>=0 && value<=1.4f)
                    *amplitude_override=value;
                else printf("ERR: amplitude must be 0..1.4 or auto\n");
            } else if (!overflow && !strcmp(line,"profiles")) {
                for (unsigned i=0;i<vvvf_profile_count();++i)
                    printf("profile %s\n",vvvf_profile_name((vvvf_profile_t)i));
            } else if (!overflow && !strncmp(line,"profile ",8)) {
                vvvf_profile_t selected;
                if (*hz>0.01f || *target>0.01f)
                    printf("ERR: stop before changing profile\n");
                else if (vvvf_profile_from_name(line+8,&selected)) {
                    *profile=selected; *braking=false; *amplitude_override=-1;
                    printf("OK: profile=%s\n",vvvf_profile_name(selected));
                } else printf("ERR: unknown profile (use profiles)\n");
            } else if (!overflow && length > 2 && line[1] == ' ' &&
                       (line[0] == 'f' || line[0] == 'r')) {
                char *end;
                float value = strtof(line+2,&end);
                if (*end == 0 && end != line+2 && isfinite(value) && value >= 0 && value <= VVVF_MAX_HZ) {
                    if (value!=*hz) *braking=value<*hz;
                    *target = value; *remote = true;
                    if (line[0] == 'f') *hz = value;
                }
            }
            length = 0; overflow = false;
        } else if (length+1 < sizeof(line)) {
            line[length++] = (char)ch;
        } else overflow = true;
    }
}
int main(void) {
    stdio_init_all();
    gpio_init(VVVF_UP_PIN); gpio_pull_up(VVVF_UP_PIN);
    gpio_init(VVVF_DOWN_PIN); gpio_pull_up(VVVF_DOWN_PIN);
    for (size_t i=0;i<vvvf_profile_count();++i)
        if (!vvvf_profile_validate(vvvf_profile_definition(i))) panic("Invalid profile definition");
    vvvf_profile_t profile;
    if (!vvvf_profile_from_name(VVVF_DEFAULT_PROFILE_NAME,&profile)) panic("Unknown default profile");
    float hz = 0, target = 0, amplitude_override=-1;
    bool remote = false, braking = false;
    vvvf_output_init(vvvf_command(hz,profile));
    uint32_t previous = time_us_32(), last_log = previous;
    while (true) {
        sleep_ms(10);
        uint32_t now = time_us_32();
        console(&hz,&target,&remote,&profile,&braking,&amplitude_override);
        bool up = !gpio_get(VVVF_UP_PIN), down = !gpio_get(VVVF_DOWN_PIN);
        if (up || down) remote = false;
        if (up != down) braking=down;
        float next = vvvf_ramp(hz,remote ? hz < target : up,
                              remote ? hz > target : down,(now-previous)*1e-6f);
        if (remote && ((hz < target && next > target) || (hz > target && next < target))) next = target;
        hz = next;
        previous = now;
        vvvf_command_t command = vvvf_command_direction(hz,profile,braking);
        if(amplitude_override>=0) vvvf_command_set_amplitude(&command,amplitude_override);
        vvvf_output_command(command);
        if (now-last_log >= 500000u) {
            last_log = now;
            printf("profile=%s direction=%s f=%.3f Hz m_cmd=%.5f m_ref=%.5f N=%u fc=%.3f wave=%s angle=%.6f render_max=%lu us fault=%u\n",
                   vvvf_profile_name(profile),braking ? "brake" : "power",hz,command.requested_modulation/32767.0f,command.modulation/32767.0f,command.pulses,
                   command.carrier_step*(100000.0/4294967296.0),vvvf_waveform_name(command.waveform),command.pulse_angle*(360.0/4294967296.0),
                   (unsigned long)vvvf_output_max_render_us(),vvvf_output_fault());
        }
    }
}
