#include "timer.h"
#include "rtc.h"
#include "process.h"

list_t *timer_interfaces;
list_t* wakeup_list;

timer_interface_t* current_timer_interface = NULL;

long internal_ticks_seconds = 0;
long internal_ticks_mseconds = 0;
long internal_ticks;
uint32_t internal_counter01;
uint32_t internal_counter02;

long timezone_sec = 0;

uint64_t year;
uint32_t month;

long timeofday;

int enable_timer_interface = 0;

void init_timer_interface()
{
    timezone_sec = 19800;
    timer_interfaces = list_create();
    wakeup_list = list_create();
    enable_timer_interface = 1;
    ticks = 0;
    internal_counter01 = current_timer_interface->frequency;
    asm("divl %%ebx" : "=a"(internal_counter02) : "a"(current_timer_interface->frequency), "b"(1000), "d"(0));
    timeofday = gettimeofday_internal() + timezone_sec;
    rtc_read(NULL, NULL, NULL, NULL, &month, &year);
}

void timer_interface_call()
{
    if(!(list_size(timer_interfaces) == 0) && (current_timer_interface != NULL) && (enable_timer_interface == 1))
    {
        if(wakeup_list != NULL)
        {
            for(register listnode_t* l = wakeup_list->head; l != NULL; l = l->next)
            {
                wakeup_info_t* w = l->val;
                if(internal_ticks >= w->ticks)
                {
                    w->ticks = internal_ticks + current_timer_interface->frequency * w->seconds;
                    w->callback();
                }
            }
        }

        if(internal_counter02 == 0)
        {
            asm("divl %%ebx" : "=a"(internal_counter02) : "a"(current_timer_interface->frequency), "b"(1000), "d"(0));
            asm("incl %0" : "+r"(internal_ticks_mseconds));
            asm("incl %0" : "+r"(current_process->ticks_since_boot));
        }

        if(internal_counter01 == 0)
        {
            internal_counter01 = current_timer_interface->frequency;
            asm("incl %0" : "+r"(internal_ticks_seconds));
            asm("incl %0" : "+r"(timeofday));
        }
        asm("decl %0" : "+r"(internal_counter01));
        asm("decl %0" : "+r"(internal_counter02));
        asm("incl %0" : "+r"(internal_ticks));
    }
}

void sleep(uint32_t ms)
{
    uint32_t ticks = internal_ticks_mseconds + ms;
    while(internal_ticks_mseconds < ticks)
    {
        asm("hlt");
    }
}

uint32_t get_frequency()
{
    if(current_timer_interface != NULL)
        return current_timer_interface->frequency;
}

void register_timer_interface(timer_interface_t* interface)
{
    interface->self = list_insert_front(timer_interfaces, interface);
    current_timer_interface = interface;
    internal_counter01 = current_timer_interface->frequency;
    internal_ticks = 0;
    asm("divl %%ebx" : "=a"(internal_counter02) : "a"(current_timer_interface->frequency), "b"(1000), "d"(0));
}

void register_wakeup_callback(wakeup_callback callback, double seconds)
{
    wakeup_info_t* w = ZALLOC_TYPES(wakeup_info_t);
    w->callback = callback;
    w->seconds = seconds;
    w->ticks = internal_ticks + seconds * current_timer_interface->frequency;
    list_push(wakeup_list, w);
}

uint32_t get_ticks()
{
    if((list_size(timer_interfaces) != 0) && (current_timer_interface != NULL) && (current_timer_interface->get_ticks != NULL))
    {
        uint32_t ret = current_timer_interface->get_ticks();
        return ret;
    }
}

void set_frequency(uint32_t hz)
{
    if((list_size(timer_interfaces) != 0) && (current_timer_interface != NULL) && (current_timer_interface->set_frequency != NULL))
    {
        current_timer_interface->set_frequency(hz);
        current_timer_interface->frequency = hz;
    }
}

void printtime()
{
    uint32_t time_s = timeofday;
    uint32_t n = time_s;

    uint32_t days = (n) / (24 * 3600);
    n = n % (24 * 3600);

    uint32_t hours = (n) / 3600;
    n = n % 3600;

    uint32_t minutes = (n) / 60;
    n = n % 60;

    uint32_t seconds = n;

    printf("%02d:%02d:%02d | DAY: %d\n", hours, minutes, seconds, days);
}

int gettimeofday(struct timeval * tp, void *tzp)
{
    tp->tv_sec = timeofday;
    tp->tv_usec = 0; // Not implemented
    return 0;
}

int isPM = 0;

void timetochar(char* str, int format)
{
        uint32_t time_s = timeofday;
    uint32_t n = time_s;

    uint32_t days = (n) / (24 * 3600);
    n = n % (24 * 3600);

    uint32_t hours = (n) / 3600;
    n = n % 3600;

    uint32_t minutes = (n) / 60;
    n = n % 60;

    uint32_t seconds = n;

    uint32_t dm = 0;
    uint32_t dl = 0;
    int isLeap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
    if(isLeap)
    {
        dl = ((year-1971) * 366);
    }
    else
    {
        dl = ((year-1971) * 365);
    }

    uint32_t di = 0;
    di += days_of_month((year-1971), month-1);
    dl += di;
    dm = dl-days;
    //serialprintf("%d:%d\n", dm, days);

    if(hours > 12)
    {
        isPM = 1;
    }

    if(format == 0)
    {
        // DD/MM/YY HH:mm:ss
        memset(str, 0, strlen(str));
        strcat(str, itoa_r(dm,10));
        strcat(str, "/");
        strcat(str, itoa_r(month,10));
        strcat(str,"/");
        strcat(str, itoa_r(year,10));
        strcat(str," ");
        strcat(str,itoa_r(hours,10));
        strcat(str,":");
        strcat(str,itoa_r(minutes,10));
        strcat(str,":");
        strcat(str,itoa_r(seconds,10));
        strcat(str, "\0");
    }
}

long gettimeofday_seconds()
{
    return timeofday;
}