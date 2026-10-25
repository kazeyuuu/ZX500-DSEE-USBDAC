package com.zx.dsee;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

/** Local root controller. Audio remains in the detached native bridge when the
 * activity is paused or closed. No network/storage/background permissions. */
public class MainActivity extends Activity {
    private static final String CONTROL="/data/local/tmp/dsee-experimental/dsee-mode.sh";
    private static final int INK=0xff173d35, MUTED=0xff65756f;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private final ExecutorService io=Executors.newSingleThreadExecutor();
    private TextView stateText, detailText, inRate, outRate, errorText, effectsText;
    private Switch enableSwitch, effectsSwitch;
    private Button retryButton, dacButton, settingsButton, effectsButton;
    private boolean busy=false, polling=false, ready=false, binding=false, resumed=false, destroyed=false;
    private int revision=0;
    private Map<String,String> latest=new HashMap<>();
    private final Runnable poll=new Runnable(){public void run(){
        if(!resumed)return;
        if(ready&&!busy&&!polling)refresh();
        ui.postDelayed(this,4000);
    }};

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        getWindow().setStatusBarColor(0xfff6f8f5);
        getWindow().setNavigationBarColor(0xfff6f8f5);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR|View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        LinearLayout page=column();page.setPadding(dp(18),dp(12),dp(18),dp(16));page.setBackgroundColor(0xfff6f8f5);
        TextView title=text("ZX DSEE",23,INK);title.setTypeface(null,Typeface.BOLD);page.addView(title);
        TextView sub=text("USB DAC · 本机原厂音效",12,MUTED);page.addView(sub,space(4));

        LinearLayout hero=column();hero.setPadding(dp(17),dp(12),dp(17),dp(12));hero.setBackground(round(INK,16));
        TextView tag=text("原厂 DSEE",11,0xffa5d8bd);hero.addView(tag);
        stateText=text("正在准备",21,Color.WHITE);stateText.setTypeface(null,Typeface.BOLD);hero.addView(stateText,space(4));
        detailText=text("首次使用请允许 KernelSU 的 root 请求。",12,0xffc5dacf);hero.addView(detailText,space(5));
        LinearLayout rates=row();rates.setPadding(0,dp(11),0,0);
        LinearLayout left=column(),right=column();
        left.addView(text("USB 输入",11,0xffa5d8bd));inRate=text("—",20,Color.WHITE);left.addView(inRate,space(3));
        right.addView(text("耳机输出",11,0xffa5d8bd));outRate=text("—",20,Color.WHITE);right.addView(outRate,space(3));
        rates.addView(left,new LinearLayout.LayoutParams(0,-2,1));rates.addView(right,new LinearLayout.LayoutParams(0,-2,1));hero.addView(rates);
        page.addView(hero,space(12));

        LinearLayout controls=column();controls.setPadding(dp(14),dp(8),dp(14),dp(9));controls.setBackground(round(Color.WHITE,14));
        enableSwitch=new Switch(this);enableSwitch.setText("启用 DSEE");enableSwitch.setTextSize(16);enableSwitch.setTextColor(INK);enableSwitch.setMinHeight(dp(40));enableSwitch.setContentDescription("启用 DSEE");controls.addView(enableSwitch);
        effectsSwitch=new Switch(this);effectsSwitch.setText("应用本机 EQ/DC");effectsSwitch.setTextSize(16);effectsSwitch.setTextColor(INK);effectsSwitch.setMinHeight(dp(40));effectsSwitch.setContentDescription("应用本机 EQ/DC");controls.addView(effectsSwitch,space(4));
        effectsText=text("跟随播放器的 EQ 和 DC 相位设置。",11,MUTED);controls.addView(effectsText,space(3));
        errorText=text("",12,0xff9b432e);errorText.setVisibility(View.GONE);controls.addView(errorText,space(12));
        page.addView(controls,space(12));

        retryButton=button("重试");retryButton.setVisibility(View.GONE);page.addView(retryButton,space(9));
        LinearLayout usb=row();dacButton=button("启用 USB DAC");settingsButton=button("音量 / USB 设置");
        usb.addView(dacButton,new LinearLayout.LayoutParams(0,dp(46),1));usb.addView(settingsButton,new LinearLayout.LayoutParams(0,dp(46),1));page.addView(usb,space(9));
        effectsButton=button("播放器音效设置");page.addView(effectsButton,space(2));
        TextView note=text("DSEE 支持 44.1 / 48 / 88.2 / 96 kHz 输入。\nEQ/DC 支持至 192 kHz；352.8 / 384 kHz 旁路。",11,MUTED);page.addView(note,space(8));
        page.addView(text("适配 ZX505 4.08 · 非官方应用",10,MUTED),space(12));
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.addView(page);setContentView(scroll);

        enableSwitch.setOnCheckedChangeListener((v,on)->{if(!binding&&!busy&&ready)changeMode(on);});
        effectsSwitch.setOnCheckedChangeListener((v,on)->{if(!binding&&!busy&&ready)changeEffects(on);});
        retryButton.setOnClickListener(v->prepare());
        dacButton.setOnClickListener(v->startDac());
        settingsButton.setOnClickListener(v->{Intent i=getPackageManager().getLaunchIntentForPackage("com.zx.dac");if(i!=null)startActivity(i);else toast("未安装原 ZX DAC 应用");});
        effectsButton.setOnClickListener(v->{Intent i=getPackageManager().getLaunchIntentForPackage("com.sony.walkman.soundeffectapp");if(i!=null)startActivity(i);else toast("请从播放器设置中打开“音效设置”");});
        prepare();
    }
    @Override protected void onResume(){super.onResume();resumed=true;if(!ready&&!busy)prepare();ui.removeCallbacks(poll);ui.postDelayed(poll,4000);}
    @Override protected void onPause(){resumed=false;ui.removeCallbacks(poll);super.onPause();}
    @Override protected void onDestroy(){destroyed=true;ui.removeCallbacksAndMessages(null);io.shutdown();super.onDestroy();}

    private void prepare(){
        if(busy)return;setBusy(true);retryButton.setVisibility(View.GONE);errorText.setVisibility(View.GONE);stateText.setText("正在准备");detailText.setText("首次使用请允许 KernelSU 的 root 请求。");
        io.execute(()->{
            try{
                File payload=extractPayload();
                Result root=su("id -u",60);
                if(!root.ok||!root.out.trim().equals("0"))throw new SetupProblem("请在 KernelSU → 超级用户中允许 ZX DSEE，然后返回此页重试。");
                if(!su("getprop ro.build.display.id",10).out.trim().equals("4.08.00"))throw new SetupProblem("此版本仅适配 ZX505 4.08.00 固件。");
                if(!su("sha256sum /vendor/lib/hw/audio.primary.icx1295.so",10).out.startsWith("dfd3481e5be322f306acef4fac08ce85b204821570597e72c541a1978ce305b8 "))throw new SetupProblem("本机音频组件不兼容，无法启用 DSEE。");
                Properties hashes=hashes();
                Result check=su("cd /data/local/tmp/dsee-experimental && sha256sum dsee-mode.sh uac2bridge original/uac2bridge",10);
                boolean matches=check.ok;
                for(String name:new String[]{"dsee-mode.sh","uac2bridge","original/uac2bridge"})matches=matches&&check.out.contains(hashes.getProperty(name)+"  "+name+"\n");
                if(!matches){
                    Result install=su("sh "+q(new File(payload,"install.sh").getAbsolutePath())+" "+q(payload.getAbsolutePath()),30);
                    if(!install.ok)throw new SetupProblem("组件准备未完成，请确认已安装兼容的 USB DAC 环境后重试。");
                }
                Result status=su("sh "+CONTROL+" appstatus",15);
                if(!status.ok||!"1".equals(parse(status.out).get("INSTALLED")))throw new SetupProblem("暂时无法读取设备状态，请重试。");
                ui.post(()->{if(destroyed)return;ready=true;render(status.out);setBusy(false);
                    if("1".equals(latest.get("USB_BOUND"))&&("1".equals(latest.get("REQUESTED"))||"1".equals(latest.get("EFFECTS_REQUESTED"))))
                        startForegroundService(new Intent(this,SonyControlService.class));
                });
            }catch(Exception e){ui.post(()->{if(destroyed)return;showSetupError(e instanceof SetupProblem?e.getMessage():"组件准备未完成，请重试。");});}
        });
    }
    private void refresh(){
        if(busy||polling)return;polling=true;
        final int requestRevision=revision;
        io.execute(()->{
            Result result=su("sh "+CONTROL+" appstatus",15);
            ui.post(()->{polling=false;if(destroyed)return;if(requestRevision==revision&&!busy){if(result.ok)render(result.out);else showSetupError("暂时无法读取设备状态，请检查 KernelSU 授权后重试。");}});
        });
    }
    private void changeMode(boolean enabled){
        setBusy(true);detailText.setText(enabled?"正在启用原厂处理器…":"正在关闭 DSEE…");
        io.execute(()->{
            Result change=su("sh "+CONTROL+(enabled?" on":" off"),30);
            Result status=su("sh "+CONTROL+" appstatus",15);
            ui.post(()->{if(destroyed)return;if(status.ok)render(status.out);else showSetupError("暂时无法读取设备状态，请重试。");setBusy(false);if(!change.ok)toast("切换未完成，请检查 USB 连接和 root 授权后重试。");else toast(enabled?"DSEE 已开启":"DSEE 已关闭");});
        });
    }
    private void changeEffects(boolean enabled){
        setBusy(true);effectsText.setText(enabled?"正在接入播放器音效…":"正在关闭 EQ/DC 同步…");
        io.execute(()->{
            Result change=su("sh "+CONTROL+(enabled?" effects-on":" effects-off"),30);
            Result status=su("sh "+CONTROL+" appstatus",15);
            ui.post(()->{if(destroyed)return;if(status.ok)render(status.out);else showSetupError("暂时无法读取设备状态，请重试。");setBusy(false);toast(change.ok?(enabled?"已应用本机 EQ/DC":"EQ/DC 同步已关闭"):"切换未完成，请重试。");});
        });
    }
    private void startDac(){
        setBusy(true);detailText.setText("正在进入 USB DAC 模式…");
        io.execute(()->{
            Result action=su("sh /data/local/tmp/usb-dac.sh dac",60);
            Result status=su("sh "+CONTROL+" appstatus",15);
            ui.post(()->{if(destroyed)return;if(status.ok)render(status.out);else showSetupError("暂时无法读取设备状态，请重试。");setBusy(false);toast(action.ok?"USB DAC 已启用":"USB DAC 未能启用，请检查连接和 root 授权。");});
        });
    }
    private void render(String raw){
        latest=parse(raw);
        binding=true;enableSwitch.setChecked("1".equals(latest.get("REQUESTED")));effectsSwitch.setChecked("1".equals(latest.get("EFFECTS_REQUESTED")));binding=false;
        inRate.setText(rate(latest.get("INPUT_RATE")));outRate.setText(rate(latest.get("OUTPUT_RATE")));
        String state=latest.get("STATE");
        if("active".equals(state)){stateText.setText("正在处理");detailText.setText("原厂 DSEE 已接入耳机输出。");}
        else if("off".equals(state)){stateText.setText("DSEE 已关闭");detailText.setText("保持电脑的输入采样率；EQ/DC 可独立使用。");}
        else if("bypass".equals(state)){stateText.setText("已开启 · 当前直通");detailText.setText("此采样率自动直通，DSEE 支持 44.1 / 48 / 88.2 / 96 kHz。");}
        else if("waiting".equals(state)){stateText.setText("已开启 · 等待 USB");detailText.setText("进入 USB DAC 模式后应用此设置。");}
        else if("idle".equals(state)){stateText.setText("已开启 · 等待音频");detailText.setText("请在电脑上播放音乐。");}
        else {stateText.setText("桥接尚未运行");detailText.setText("可启用 USB DAC 或关闭 DSEE 恢复原桥接。");}
        String xr=latest.get("XRUN"),clips=latest.get("CLIPS");
        boolean eq="1".equals(latest.get("EQ_ACTIVE")),dc="1".equals(latest.get("DC_ACTIVE"));
        if(!effectsSwitch.isChecked())effectsText.setText("EQ/DC 同步已关闭。");
        else if("1".equals(latest.get("FX_DIRECT")))effectsText.setText("本机已开启“直通音源”，EQ/DC 旁路。");
        else if("1".equals(latest.get("FX_UNAVAILABLE")))effectsText.setText(positive(latest.get("FX_RATE"))&&Integer.parseInt(latest.get("FX_RATE"))>192000?"当前采样率下 EQ/DC 自动旁路。":"等待播放器音效设置就绪。");
        else if(!"RUNNING".equals(latest.get("PLAYBACK_STATE")))effectsText.setText("等待 USB 音频；设置将自动应用。");
        else effectsText.setText("EQ · "+(eq?"开启":"关闭")+"    DC · "+(dc?dcName(latest.get("DC_TYPE")):"关闭"));
        boolean audioProblem=("active".equals(state)||eq||dc)&&(positive(xr)||positive(clips));
        errorText.setText(audioProblem?"播放出现异常，可关闭音效恢复直通。":"");errorText.setVisibility(audioProblem?View.VISIBLE:View.GONE);
    }
    private static String dcName(String s){String[] names={"A 低","A 标准","A 高","B 低","B 标准","B 高","C 低","C 标准","C 高"};try{return names[Integer.parseInt(s)];}catch(Exception e){return "开启";}}
    private static boolean positive(String s){try{return Long.parseLong(s)>0;}catch(Exception e){return false;}}
    private void showSetupError(String message){
        ready=false;stateText.setText("尚未就绪");detailText.setText(message);inRate.setText("—");outRate.setText("—");retryButton.setVisibility(View.VISIBLE);setBusy(false);
    }
    private void setBusy(boolean value){
        if(value&&!busy)++revision;
        busy=value;enableSwitch.setEnabled(ready&&!busy);effectsSwitch.setEnabled(ready&&!busy);retryButton.setEnabled(!busy);effectsButton.setEnabled(!busy);
        dacButton.setEnabled(ready&&!busy&&!"1".equals(latest.get("USB_BOUND")));settingsButton.setEnabled(!busy);
    }
    private File extractPayload() throws Exception{
        Properties expected=hashes();File base=new File(getFilesDir(),"payload");
        for(String name:expected.stringPropertyNames()){
            File out=new File(base,name);File parent=out.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new Exception("无法建立组件目录");
            MessageDigest digest=MessageDigest.getInstance("SHA-256");
            try(InputStream in=getAssets().open("payload/"+name);FileOutputStream f=new FileOutputStream(out)){
                byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1){digest.update(b,0,n);f.write(b,0,n);}
            }
            StringBuilder actual=new StringBuilder();for(byte b:digest.digest())actual.append(String.format(Locale.ROOT,"%02x",b&255));
            if(!actual.toString().equals(expected.getProperty(name)))throw new Exception("组件校验失败："+name);
        }
        return base;
    }
    private Properties hashes() throws Exception{Properties p=new Properties();try(InputStream in=getAssets().open("payload-hashes.properties")){p.load(in);}return p;}
    private static String q(String s){return "'"+s.replace("'","'\\''")+"'";}
    private static Map<String,String> parse(String s){Map<String,String> m=new HashMap<>();for(String line:s.split("\n")){int n=line.indexOf('=');if(n>0)m.put(line.substring(0,n).trim(),line.substring(n+1).trim());}return m;}
    private static String rate(String s){try{int n=Integer.parseInt(s);return n%1000==0?(n/1000)+" kHz":String.format(Locale.ROOT,"%.1f kHz",n/1000.0);}catch(Exception e){return "—";}}
    private void toast(String s){Toast.makeText(this,s,Toast.LENGTH_LONG).show();}
    private int dp(int n){return Math.round(n*getResources().getDisplayMetrics().density);}
    private LinearLayout column(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.VERTICAL);return l;}
    private LinearLayout row(){LinearLayout l=new LinearLayout(this);l.setOrientation(LinearLayout.HORIZONTAL);return l;}
    private TextView text(String s,int size,int color){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(color);return t;}
    private Button button(String s){Button b=new Button(this);b.setText(s);b.setTextColor(INK);b.setTextSize(12);b.setAllCaps(false);b.setContentDescription(s);return b;}
    private LinearLayout.LayoutParams space(int margin){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.WRAP_CONTENT);p.topMargin=dp(margin);return p;}
    private GradientDrawable round(int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));return d;}
    private static class Result{final boolean ok;final String out;Result(boolean ok,String out){this.ok=ok;this.out=out;}}
    private static class SetupProblem extends Exception{SetupProblem(String message){super(message);}}
    private static Result su(String command,int seconds){
        try{
            Process p=new ProcessBuilder("su","-c",command).redirectErrorStream(true).start();
            ByteArrayOutputStream output=new ByteArrayOutputStream();
            Thread reader=new Thread(()->{try(InputStream in=p.getInputStream()){byte[] b=new byte[4096];int n;while((n=in.read(b))!=-1){synchronized(output){if(output.size()<32768)output.write(b,0,Math.min(n,32768-output.size()));}}}catch(Exception ignored){}});
            reader.setDaemon(true);reader.start();boolean done=p.waitFor(seconds,TimeUnit.SECONDS);
            if(!done){p.destroy();reader.join(1000);return new Result(false,"root 操作超时，请检查 KernelSU 授权。");}
            reader.join(2000);synchronized(output){return new Result(p.exitValue()==0,output.toString(StandardCharsets.UTF_8.name()));}
        }catch(Exception e){return new Result(false,"root 请求失败："+e.getMessage());}
    }
}
