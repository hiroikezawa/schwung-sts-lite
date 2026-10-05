import assert from "node:assert/strict";
import fs from "node:fs";
import {pathToFileURL} from "node:url";
/* The UI runs the device's own page controller, so the test does too: point
 * SCHWUNG_ROOT at a Schwung checkout (default: a sibling ../schwung). */
const root=process.env.SCHWUNG_ROOT
    ? pathToFileURL(process.env.SCHWUNG_ROOT.replace(/\/?$/,"/"))
    : new URL("../../schwung/",import.meta.url);
const from=p=>import(new URL(p,root));
const {createController}=await from("src/shared/param_pages/page_controller.mjs");
const {decodeInput,applyInput}=await from("src/shared/param_pages/page_input.mjs");
const {createFramebuffer,drawContext}=await from("tools/param-pages/harness.mjs");
import {createUi,noteTable} from "../ui_core_v0210.mjs";
const meta=JSON.parse(fs.readFileSync(new URL("../module.json",import.meta.url)));
const keys=["volume","pitch"];
const hierarchy={modes:null,levels:{
 root:{label:"STS Lite",knobs:keys,params:[...keys.filter(Boolean),{level:"scale",label:"Scale"}]},
 scale:{label:"Scale",knobs:["base_pitch","root","scale","length"],params:["base_pitch","root","scale","length"]}
}};
const values=Object.fromEntries(meta.capabilities.chain_params.map(x=>[x.key,String(x.default)]));
values.edit_channel="0";
values.write="0";
values.sample="0";
values.pitch_range="60";
let clock=0,visible=true,gates="1".repeat(16),touched=false,motion=0,effective=14;
const writes=[],reads=[],leds=[],blocks=[];
const get=k=>{
 reads.push(k);
 if(k==="grid_hierarchy")return JSON.stringify(hierarchy);
 if(k==="chain_params")return JSON.stringify(meta.capabilities.chain_params);
 if(k==="performance_status")return [values.edit_channel,values.write,values.length==="1"?16:8,0,-24,values.root,values.scale,values.base_pitch,values.pitch_range,gates,motion,values.sample,playing].join(",");
 if(k==="eff_pitch")return String(effective);
 return values[k]??"";
};
const set=(k,v)=>{
 v=String(v);writes.push([k,v]);if(k in values)values[k]=v;
 if(k==="touch")touched=v==="pitch,1";
 if(k==="gate_toggle"){const a=gates.split("");a[+v]=a[+v]==="1"?"0":"1";gates=a.join("");}
};
const fb=createFramebuffer();
let playing=-24;
const ui=createUi({createController,decodeInput,applyInput,getParam:get,setParam:set,announce:()=>{},
 now:()=>clock,layout:()=>"movy",draw:drawContext(fb),clear:()=>fb.clearScreen(),
 trailingMenus:()=>[],runAction:()=>false,shiftHeld:()=>false,displayOn:()=>visible,
 padBlock:v=>blocks.push(v),sendLed:(n,c)=>leds.push([n,c]),padSnapshot:()=>({"68":17})});
function tick(n=8){for(let i=0;i<n;++i){clock+=17;ui.tick();}}
function press(n){ui.onMidi([144,n,127]);ui.onMidi([128,n,0]);}
ui.init();tick(30);
assert.deepEqual(ui.state.controller.page.keys.map(k=>k.split(":").at(-1)),["volume","pitch"],
 "padding must never persist in the controller's contract");
assert.equal(ui.state.controller.keyAt(0).split(":").at(-1),"volume");
assert.equal(ui.state.controller.keyAt(7),null,"K8 is mapped only during input/render");
assert.equal(blocks.at(-1),true);
assert.equal(fb.clipped(),0);
assert(fb.countLit()>100);
assert.equal(leds.filter(x=>x[0]===99).at(-1)[1],65);
for(let slot=0;slot<8;++slot){
 ui.onMidi([144,slot,127]);tick(2);
 ui.onMidi([128,slot,0]);tick(2);
}
press(99);tick();assert.equal(values.write,"1");
assert.equal(leds.filter(x=>x[0]===99).at(-1)[1],127);
press(98);assert(writes.some(x=>x[0]==="randomize"));
press(91);assert.equal(values.edit_channel,"1");
ui.onMidi([176,77,1]);assert.equal(values.edit_channel,"1","K7 is unassigned");
const beforeK2=writes.length;
ui.onMidi([176,72,1]);assert.equal(writes.length,beforeK2,"K2 is unassigned");
press(91);assert.equal(values.edit_channel,"0","pad 91 switches the edit sound");
ui.onMidi([144,84,127]);
assert(ui.state.samplePick);
press(76);assert.equal(values.sample,"8","pad 76 selects sample 9");
press(77);assert.equal(values.sample,"9","pad 77 selects sample 10");
assert.equal(gates[8],"1","sample picker must not toggle 16-step gates");
ui.onMidi([128,84,0]);assert(!ui.state.samplePick);
press(68);assert.equal(gates[0],"0");
const before=writes.length;press(76);assert.equal(writes.length,before);
set("length",1);tick();press(83);assert.equal(gates[15],"0");
ui.onMidi([144,7,127]);assert(touched);
const p0=Number(values.pitch); /* module.json default: K8 steps from it */
const readsBeforeTurn=reads.length;
ui.onMidi([176,78,1]);assert.equal(values.pitch,String(p0+1));
assert(reads.length-readsBeforeTurn<=2,"K8 turn must not re-read the whole page");
tick(12);assert(touched);
ui.onMidi([176,78,4]);tick(2);
assert.equal(values.pitch,String(p0+5),"fast clockwise turn must keep all four detents");
ui.onMidi([176,78,125]);tick(2);
assert.equal(values.pitch,String(p0+2),"fast counter-clockwise turn must keep all three detents");
tick(1);
const releaseWrites=writes.length;
ui.onMidi([176,78,1]);
assert.equal(values.pitch,String(p0+3));
ui.onMidi([176,78,1]);
assert.equal(values.pitch,String(p0+3),"last turn should be pending inside throttle window");
ui.onMidi([128,7,0]);assert(!touched);
assert.equal(values.pitch,String(p0+4),"releasing K8 must flush the final pitch");
const releaseOrder=writes.slice(releaseWrites).map(([key,value])=>key+"="+value);
assert(releaseOrder.indexOf("pitch="+(p0+4))>=0);
assert(releaseOrder.indexOf("pitch="+(p0+4))<releaseOrder.indexOf("touch=pitch,0"),
 "final pitch must reach the DSP before touch-off closes recording");
motion=1;effective=5;playing=-48;tick(30);
const headerPixels=()=>{let bits="";for(let y=0;y<8;++y)for(let x=0;x<128;++x)
 bits+=fb.pixels[y*128+x];return bits;};
const lowHeader=headerPixels();
assert(ui.state.controller.isModulatedCached("pitch"),"K8 shows recorded pitch motion");
assert.equal(ui.state.controller.state.modValues.pitch,"5");
assert(reads.includes("eff_pitch"),"K8 reads the currently playing pitch");
tick(40);
const k8Pixels=()=>{let bits="";for(let y=29;y<55;++y)for(let x=96;x<128;++x)
 bits+=fb.pixels[y*128+x];return bits;};
const lowPitchPixels=k8Pixels();
effective=22;playing=-12;tick(6);
assert.notEqual(headerPixels(),lowHeader,"header tracks the sounding pitch");
assert.equal(ui.state.controller.state.modValues.pitch,"22","K8 follows the next step");
assert.notEqual(k8Pixels(),lowPitchPixels,"the K8 knob graphic must move with playback");
ui.onMidi([176,14,1]);tick(20);
assert.deepEqual(ui.state.controller.page.keys.map(k=>k.split(":").at(-1)),["base_pitch","root","scale","length"]);
ui.onMidi([176,14,127]);tick();
ui.onMidi([144,7,127]);assert(touched);
press(91);assert(!touched);
for(let i=0;i<80;++i){
 const slot=i%8;
 ui.onMidi([144,slot,127]);
 ui.onMidi([176,71+slot,i%3===0?127:1]);
 tick(1);
 ui.onMidi([128,slot,0]);
 tick(1);
}
assert.deepEqual(ui.state.controller.page.keys.map(k=>k.split(":").at(-1)),["volume","pitch"],
 "touch and re-settle must leave the controller contract intact");
visible=false;ui.tick();assert.equal(blocks.at(-1),false);
assert(leds.some(([n,c])=>n===68&&c===17));
assert.deepEqual(noteTable(-24,60,0,4).slice(0,8),[-53,-52,-50,-48,-47,-45,-43,-41]);
console.log("PASS: real page controller, pads, LEDs, touch, degree stepping, page 2, exit cleanup");
