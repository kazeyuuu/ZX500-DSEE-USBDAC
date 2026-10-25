/* Control-thread IPC only; never run Binder or mixer I/O on the PCM thread. */
#define SONY_CONTROL_FILE "/data/user/0/com.zx.dsee/files/usb-controls"
static void sony_publish_controls(int host,int ceiling,int mute){
 struct mixer *m=mixer_open(g_play_card);
 struct mixer_ctl *v=m?mixer_get_ctl_by_name(m,HW_VOL_NAME):NULL;
 int actual=v?mixer_ctl_get_value(v,0):-1;
 if(m)mixer_close(m);
 if(actual<0||actual>120)return;
 /* App owns this precreated file and its SELinux label. Do not create it. */
 int fd=open(SONY_CONTROL_FILE,O_WRONLY|O_TRUNC|O_CLOEXEC);
 if(fd<0)return;
 char text[192];int n=snprintf(text,sizeof(text),"pid=%d\ntime=%ld\nhost=%d\nceiling=%d\nactual=%d\nmute=%d\nEND\n",getpid(),(long)time(NULL),host,ceiling,actual,mute);
 if(n>0&&(size_t)n<sizeof(text)){ssize_t ignored=write(fd,text,(size_t)n);(void)ignored;}
 close(fd);
}
