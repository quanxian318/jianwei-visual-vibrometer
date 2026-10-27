const fs=require('fs');const vm=require('vm');const path=require('path');
const base=path.resolve(__dirname,'../../06_软件与FPGA/mobile_app/web/js');
(async()=>{
 const pages={},commands=[];const els={'#sVoice':{checked:true},'#sBuzz':{checked:false}};
 const ctx={setTimeout,clearTimeout,console,window:{},App:{register:(n,p)=>pages[n]=p,dev:{command:async(c,a)=>{commands.push({cmd:c,args:a});return{ok:true}}},poll:()=>{}},U:{$:s=>els[s]||null,$$:()=>[],toast:()=>{},haptic:()=>{},store:{get:()=>true}},Icon:()=>''};
 vm.createContext(ctx);vm.runInContext(fs.readFileSync(path.join(base,'page_settings.js'),'utf8'),ctx);
 pages.settings.el={};pages.settings.s={};pages.settings.bind();
 els['#sVoice'].onchange(); await new Promise(r=>setTimeout(r,30));els['#sBuzz'].onchange();await new Promise(r=>setTimeout(r,500));
 const report={settings_changed:['voice=true','buzzer=false'],commands_sent:commands,reproduced_lost_setting:commands.length===1&&!('voice' in commands[0].args)};
 // Run the actual record detail renderer against the acoustic structure written by the PS.
 const nodes={};const el=s=>nodes[s]||(nodes[s]={innerHTML:'',textContent:'',style:{},addEventListener:()=>{}});
 const p2={};const rc={console,window:{},App:{register:(n,p)=>p2[n]=p,dev:{caps:{files:true}},pushHeader:()=>''},U:{$:el,$$:()=>[],esc:String,num:String,hz:String,timeText:String,level:()=>({c:'normal'}),levelPill:()=>'',haptic:()=>{}},Icon:()=>'',Charts:{Chart:class{set(){} }},FX3D:{Waterfall:class{set(){}}}};
 vm.createContext(rc);vm.runInContext(fs.readFileSync(path.join(base,'page_records.js'),'utf8'),rc);
 const d=p2.record;d.el={};d.id='test';d.view='wave';d.d={id:'test',record:{measurement_id:'test',result:{unit:'px',valid:true,peak_clear:true,dominant_frequency_hz:10},acoustic:{available:true,valid:true,visual:{relation:'differ',visual_hz:10,acoustic_hz:20}}},waveform:{t:[0,1],v:[0,1]},spectrum:{f:[0,10],a:[0,1]}};
 d.plot=()=>{};d.draw();report.acoustic_record={available:true,valid:true,visual:{relation:'differ',acoustic_hz:20}};report.acoustic_row_shown=nodes['#dBody'].innerHTML.includes('声学参考');report.reproduced_hidden_acoustic=!report.acoustic_row_shown;
 fs.writeFileSync(path.join(__dirname,'app_contract_probes.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
})();
