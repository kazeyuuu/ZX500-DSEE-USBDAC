package com.zx.dsee;

import android.app.*;
import android.content.*;
import android.os.*;
import java.io.*;
import java.util.Properties;

/** Uses the device's installed Sony service; never bundles Sony classes. */
public final class SonyControlService extends Service {
    private IBinder sony;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private boolean bound;
    private int lastHost=-1,lastPid=-1;
    private long lastLive=SystemClock.elapsedRealtime();
    private void syncVolume() throws Exception {
        File file=new File(getFilesDir(),"usb-controls");
        String text;
        try(FileInputStream in=new FileInputStream(file)){byte[] b=new byte[512];int n=in.read(b);text=n<0?"":new String(b,0,n,"UTF-8");}
        if(!text.endsWith("END\n"))return;
        Properties p=new Properties();p.load(new StringReader(text));
        long age=System.currentTimeMillis()/1000-Long.parseLong(p.getProperty("time"));
        if(age<0||age>5)return;
        lastLive=SystemClock.elapsedRealtime();
        int host=Integer.parseInt(p.getProperty("host")),actual=Integer.parseInt(p.getProperty("actual"));
        int ceiling=Integer.parseInt(p.getProperty("ceiling")),pid=Integer.parseInt(p.getProperty("pid"));
        if(host<0||ceiling>120||host>ceiling||actual<0||actual>120||pid<=0)return;
        if(pid!=lastPid||host!=lastHost){
            // Preserve a lower device level on connection; a subsequent host
            // change becomes the shared Sony/device level before the next key.
            int target=pid!=lastPid?Math.min(actual,host):host;
            if(call(4)!=target)call(3,target);
            if(call(4)==target){lastHost=host;lastPid=pid;}
        }
    }
    private int call(int code,int... args) throws Exception {
        Parcel in=Parcel.obtain(),out=Parcel.obtain();
        try {
            in.writeInterfaceToken("com.sony.walkman.izmaudiomanager.IIzmAudioManager");
            for(int arg:args)in.writeInt(arg);
            if(!sony.transact(code,in,out,0))throw new IOException("Unsupported Sony service");
            out.readException();return out.dataAvail()>=4?out.readInt():0;
        } finally {in.recycle();out.recycle();}
    }
    private final ServiceConnection connection=new ServiceConnection(){
        public void onServiceDisconnected(ComponentName n){sony=null;}
        public void onServiceConnected(ComponentName n,IBinder binder){sony=binder;handler.removeCallbacks(poll);handler.post(poll);}
    };
    private final Runnable poll=new Runnable(){public void run(){
        if(sony!=null)try {
            syncVolume();
            StringBuilder s=new StringBuilder();
            s.append("time=").append(System.currentTimeMillis()/1000).append('\n');
            s.append("volume=").append(call(4)).append('\n');
            s.append("preset=").append(call(43)).append('\n');
            s.append("eq=").append(call(41)).append('\n');
            s.append("dc=").append(call(53)).append('\n');
            s.append("type=").append(call(55)).append('\n');
            s.append("direct=").append(call(63)).append('\n');
            for(int band=0;band<10;band++)s.append("gain").append(band).append('=').append(call(45,band)).append('\n');
            s.append("END\n");
            File next=new File(getFilesDir(),"sony-settings.next");
            try(FileOutputStream stream=new FileOutputStream(next)){stream.write(s.toString().getBytes("UTF-8"));}
            if(!next.renameTo(new File(getFilesDir(),"sony-settings")))throw new IOException("Snapshot rename failed");
        }catch(Exception e){android.util.Log.e("ZX-SonyControl","Cannot read Sony settings",e);}
        if(SystemClock.elapsedRealtime()-lastLive>30000){stopSelf();return;}
        handler.postDelayed(this,200);
    }};
    @Override public void onCreate(){
        super.onCreate();
        try{new File(getFilesDir(),"usb-controls").createNewFile();}catch(IOException e){stopSelf();return;}
        NotificationManager manager=getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel("sony-control","USB 音效控制",NotificationManager.IMPORTANCE_LOW));
        startForeground(31,new Notification.Builder(this,"sony-control").setSmallIcon(android.R.drawable.ic_media_play).setContentTitle("ZX DSEE").setContentText("正在同步播放器音效").build());
        Intent intent=new Intent("com.sony.walkman.izmaudiomanager.IzmAudioManager");
        intent.setComponent(new ComponentName("com.sony.walkman.izmaudiomanager","com.sony.walkman.izmaudiomanager.IzmAudioManager"));
        bound=bindService(intent,connection,BIND_AUTO_CREATE);
        if(!bound)stopSelf();
    }
    @Override public int onStartCommand(Intent intent,int flags,int id){lastLive=SystemClock.elapsedRealtime();return START_NOT_STICKY;}
    @Override public void onDestroy(){handler.removeCallbacksAndMessages(null);if(bound)unbindService(connection);super.onDestroy();}
    @Override public IBinder onBind(Intent intent){return null;}
}

