/* Dedicated Chain UI built on Schwung's shared page controller.
 * Keep the imported filename versioned: QuickJS caches relative imports. */
const NAMES=["C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"];
const MASKS=[0xfff,0xab5,0x5ad,0x6ad,0x5ab,0xad5,0x6b5,0x56b,0x295];
export function noteTable(base,range,root,scale) {
    const out=[];
    for(let n=Math.max(-60,base-range/2);n<=Math.min(60,base+range/2);++n)
        if((MASKS[scale]||MASKS[0])&(1<<((n-root+120)%12)))out.push(n);
    return out;
}
export function createUi(d) {
    const bare=k=>String(k).replace(/^synth:/,"");
    const st={controller:null,channel:0,write:0,len:8,step:-1,pitch:-24,root:0,scale:4,
        base:-24,range:60,gates:"1".repeat(16),held:new Set(),pitchHeld:false,
        visible:false,tick:0,cursor:0,lastBeat:0,shift:false,contractDue:0,
        motion:0,sample:0,playing:-24,samplePick:false};
    function poll(){
        const p=String(d.getParam("performance_status")||"").split(",");
        if(p.length<10)return;
        [st.channel,st.write,st.len,st.step,st.pitch,st.root,st.scale,st.base,st.range]=p.slice(0,9).map(Number);
        st.gates=p[9];
        st.motion=Number(p[10])||0;
        st.sample=Number(p[11])||0;
        st.playing=Number(p[12]);
    }
    function releaseTouch(){
        if(st.pitchHeld){
            /* The shared controller may still hold the last fast encoder
             * value for its 20 ms write throttle. Release flushes it; keep
             * the DSP writer armed until that final pitch reaches the step. */
            withMainKnobs(()=>st.controller?.onKnobTouch(7,false));
            d.setParam("touch","pitch,0");
        }
        st.pitchHeld=false;
    }
    function releaseAll(){
        releaseTouch(); st.held.clear();st.samplePick=false;
        const snap=d.padSnapshot();
        for(let n=68;n<=99;++n)d.sendLed(n,snap?.[String(n)]??0);
        d.padBlock(false); st.visible=false;
    }
    function set(key,value){
        key=bare(key);
        if(key==="edit_channel")releaseTouch();
        d.setParam(key,value);
        if(key==="edit_channel")st.channel=Number(value)||0;
        if(key==="sample")st.sample=Number(value)||0;
        if(key==="write")st.write=Number(value)||0;
        if(key==="gate_toggle"){
            const step=Number(value);
            if(step>=0&&step<st.len){
                const bits=st.gates.split("");
                bits[step]=bits[step]==="1"?"0":"1";
                st.gates=bits.join("");
            }
        }
        if(key==="root")st.root=Number(value)||0;
        if(key==="scale")st.scale=Number(value)||0;
        if(key==="base_pitch")st.base=Number(value)||0;
        if(key==="pitch_range")st.range=Number(value)||0;
        if(key==="edit_channel")st.controller?.revalue();
        if(["root","scale","base_pitch","pitch_range"].includes(key))
            st.contractDue=d.now()+250;
    }
    /* The shared planner compacts null authored knobs. Temporarily pad only
     * input and draw calls, never the controller's stored page: its contract
     * resettler assumes every permanent page key is a non-null parameter. */
    function withMainKnobs(run){
        const page=st.controller?.page;
        if(page?.kind==="knobs"&&page.level==="root"&&page.keys?.length===2
            &&bare(page.keys[0])==="volume"&&bare(page.keys[1])==="pitch"){
            const keys=page.keys;
            page.keys=[keys[0],null,null,null,null,null,null,keys[1]];
            try{return run();}
            finally{page.keys=keys;}
        }
        return run();
    }
    function init(){
        st.controller=d.createController({
            getParam:k=>{
                const key=bare(k);
                if(key==="ui_hierarchy")return d.getParam("grid_hierarchy");
                if(key==="pitch:base")return d.getParam("pitch");
                if(key==="pitch:effective")return d.getParam("eff_pitch");
                return d.getParam(key);
            },
            setParam:set,announce:d.announce,now:d.now,
            isModulated:k=>bare(k)==="pitch"&&!!st.motion,
            formatValue:(key,raw,surface)=>{
                if(bare(key)!=="pitch")return null;
                const table=noteTable(st.base,st.range,st.root,st.scale);
                const n=table[Math.max(0,Math.min(table.length-1,Number(raw)||0))];
                /* A touched knob replaces the normal page title in the shared
                 * renderer. Keep the playing pitch visible there too. */
                if(surface==="header"&&st.motion&&Number.isFinite(st.playing))
                    return n+">"+st.playing+"st";
                return NAMES[(n+120)%12]+" "+n+"st";
            },
            trailingMenus:d.trailingMenus
        });
        poll();st.controller.load({slot:0,component:"synth",prefix:"synth"});
        if(d.displayOn()){d.padBlock(true);st.visible=true;}
    }
    function paint(){
        for(let i=0;i<8;++i){
            const n=68+(st.cursor++%32),step=n-68;
            let color=0;
            if(st.samplePick&&n<=77)color=step===st.sample?120:14;
            else if(n<=83&&step<st.len)color=step===st.step?120:st.gates[step]==="1"?11:1;
            if(n===84)color=st.samplePick?14:91;
            if(n===99)color=st.write?127:65;
            if(n===98)color=st.held.has(n)?120:21;
            if(n===91)color=st.channel?45:11;
            d.sendLed(n,color);
        }
    }
    function tick(){
        if(!d.displayOn()){if(st.visible)releaseAll();return;}
        st.visible=true;d.padBlock(true);
        if(++st.tick%3===0)poll();
        if(st.pitchHeld&&d.now()-st.lastBeat>=150){
            d.setParam("touch","pitch,1");st.lastBeat=d.now();
        }
        const c=st.controller;if(!c)return;
        if(st.contractDue&&d.now()>=st.contractDue){
            st.contractDue=0;
            c.reloadIfChanged({slot:0,component:"synth",prefix:"synth"});
        }
        c.setLayout(d.layout());c.tick();d.clear();
        const label=st.samplePick
            ?"SAMPLE "+(st.channel?"B":"A")+" "+String(st.sample+1).padStart(2,"0")+"/10"
            :(st.channel?"B":"A")+String(st.sample+1).padStart(2,"0")+
                " P"+(Number.isFinite(st.playing)?st.playing:st.pitch)+
                (st.step>=0?" "+(st.step+1)+"/"+st.len:" STOP")+
                (st.write?(st.pitchHeld?" REC":" ARM"):"");
        withMainKnobs(()=>{
            c.render(d.draw,{title:label});
            c.renderOverlays(d.draw,{clearScreen:d.clear});
        });
        paint();
    }
    function onMidi(data){
        const c=st.controller;
        if(!c||!data||data.length<3||!d.displayOn())return;
        const t=data[0]&240,a=data[1],b=data[2],down=t===144&&b>0;
        if((t===144||t===128)&&a>=68&&a<=99){
            if(!down){
                st.held.delete(a);
                if(a===84)st.samplePick=false;
                return;
            }
            if(st.held.has(a))return;st.held.add(a);
            if(a===84)st.samplePick=true;
            else if(st.samplePick&&a>=68&&a<=77)set("sample",a-68);
            else if(a===99)set("write",st.write?0:1);
            else if(a===98){releaseTouch();set("randomize",1);}
            else if(a===91)set("edit_channel",st.channel?0:1);
            else if(!st.samplePick&&a<=83&&a-68<st.len)set("gate_toggle",a-68);
            paint();return;
        }
        if(t===176&&a===49)st.shift=b>0;
        const intent=d.decodeInput(data,{shift:st.shift||d.shiftHeld()});
        if(!intent)return;
        withMainKnobs(()=>{
            if(intent.type==="touch"){
                if(!intent.down&&intent.slot===7){releaseTouch();return;}
                else if(intent.down&&bare(c.keyAt(intent.slot))==="pitch"){
                    st.pitchHeld=true;st.lastBeat=d.now();d.setParam("touch","pitch,1");
                }
            }
            /* decodeInput keeps the encoder's full relative delta, but the
             * generic applyInput reduces every knob turn to its sign. Fast
             * K8 turns carry multiple detents in one CC; dropping them makes
             * a large gesture record the same pitch on adjacent steps. The
             * controller itself accepts the full delta and fine mode keeps
             * each detent equal to one scale degree. */
            if(intent.type==="knob"&&bare(c.keyAt(intent.slot))==="pitch"){
                c.onKnobTurn(intent.slot,intent.direction,d.now(),{fine:true});
                return;
            }
            if(t===176&&a===14)releaseTouch();
            const todo=d.applyInput(c,intent,{nowMs:d.now(),reveal:st.shift});
            if(todo?.action==="menu"&&todo.entry?.action)d.runAction(todo.entry.action);
        });
    }
    function handleBack(){
        releaseTouch();
        const todo=d.applyInput(st.controller,{type:"back"},{nowMs:d.now()});
        if(todo?.action==="exit"){releaseAll();return false;}
        return true;
    }
    return {init,tick,onMidi,handleBack,releaseAll,state:st};
}
