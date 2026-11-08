const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../tools/monitor.html'), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];
new Function(script);
// Exercise counting-zone validation and trial accounting without hardware.
const start = script.indexOf('function validGate(');
const end = script.indexOf('\nfunction setGate(',start);
const gateContext={};
vm.runInNewContext(script.slice(start,end),gateContext);
const gate={vertical:false,line:.5,band:.06,positive_entry:true,roi_min:.05,roi_max:.95};
assert.equal(gateContext.validGate(gate),true);
assert.equal(gateContext.validGate({...gate,line:.02}),false);
assert.equal(gateContext.validGate({...gate,roi_min:.9}),false);
assert.equal(gateContext.validGate({...gate,line:NaN}),false);
assert.match(html,/<title>FlowCount · DeAI Labs<\/title>/);
assert.match(html,/href="http:\/\/flowcount\.local\/"/);
assert.match(html,/href="https:\/\/deai\.science\/"/);
// Pending changes must be visible beside the image, and must not be mistaken
// for the calibration used by a new acceptance trial.
assert.match(html,/id="pendingCalibration"/);
const pendingContext={settingsDirty:true,scoreDirty:false,bodyFilterDirty:false,
  updateSaveDock:()=>{},
  $:()=>pendingContext.element,element:{hidden:true}};
vm.runInNewContext(script.slice(script.indexOf('function calibrationPending('),script.indexOf('\nasync function saveZone(')),pendingContext);
pendingContext.calibrationPending();assert.equal(pendingContext.element.hidden,false);
pendingContext.settingsDirty=false;pendingContext.calibrationPending();assert.equal(pendingContext.element.hidden,true);
pendingContext.scoreDirty=true;pendingContext.calibrationPending();assert.equal(pendingContext.element.hidden,false);
const pendingTrial={settingsDirty:true,scoreDirty:false,bodyFilterDirty:false,
  message:()=>{pendingTrial.warned=true;},$:()=>({scrollIntoView:()=>{}})};
vm.runInNewContext(script.slice(script.indexOf('function startTrial('),script.indexOf('\nfunction trialDeltas(')),pendingTrial);
pendingTrial.startTrial();assert.equal(pendingTrial.warned,true);
const trialStart=script.indexOf('function trialDeltas(');
const trialEnd=script.indexOf('\nfunction renderTrial(',trialStart);
const trialContext={trial:{entries:7,exits:3,ms:1000},latest:{entries:9,exits:4,ms:2000}};
vm.runInNewContext(script.slice(trialStart,trialEnd),trialContext);
assert.equal(trialContext.trialDeltas().entries,2);
assert.equal(trialContext.trialDeltas().exits,1);
trialContext.latest.ms=10;
assert.equal(trialContext.trialDeltas(),null);
assert.equal(trialContext.trial.entries,7); // Trial reset never resets device totals.
trialContext.latest={entries:9,exits:4,ms:5000,boot_id:2};
trialContext.trial.boot_id=1;
assert.equal(trialContext.trialDeltas(),null); // A different boot is not a continuation of the trial.
const choicesStart=script.indexOf('function renderNetworkChoices(');
const choicesEnd=script.indexOf("\n$('wifiNetworks')",choicesStart);
const choices={wifiNetworks:{children:[],replaceChildren(){this.children=[];},append(x){this.children.push(x);}},networkChoices:{hidden:true},scanMessage:{textContent:''}};
const choiceContext={$:id=>choices[id],document:{createElement:()=>({})}};
vm.runInNewContext(script.slice(choicesStart,choicesEnd),choiceContext);
choiceContext.renderNetworkChoices({networks:[{ssid:'Office_24',rssi:-40},{ssid:'Office-24',rssi:-70}],error:''});
assert.equal(choices.wifiNetworks.children[1].value,'Office_24');
assert.equal(choices.wifiNetworks.children[2].value,'Office-24');
assert.equal(choices.networkChoices.hidden,false);
choiceContext.renderNetworkChoices({networks:[],error:'Radio unavailable'});
assert.equal(choices.scanMessage.textContent,'Radio unavailable');
const launcher = script.slice(script.indexOf("if(location.protocol==='file:')"));

async function check(protocol, available) {
  const elements = { dashboard: {hidden:false}, mainNav:{hidden:false}, fileHelp:{hidden:true}, fileStatus:{textContent:''} };
  let redirected = '', polled = false, streamed = false;
  vm.runInNewContext(launcher, {
    location:{ protocol, hash:'', replace:url=>{redirected=url;} },
    $:id=>elements[id],
    fetch:()=>available ? Promise.resolve({}) : Promise.reject(new Error('offline')),
    AbortSignal:{timeout:()=>({})},
    poll:()=>{polled=true;},
    frameLoop:()=>{streamed=true;},
    switchView:()=>{},
  });
  await new Promise(resolve=>setImmediate(resolve));
  return {elements, redirected, polled, streamed};
}
(async()=>{
  // If a slider is edited while its command is in flight, retain the newer
  // unsaved draft instead of silently treating it as the saved configuration.
  const zoneStart=script.indexOf('async function saveZone(');
  const zoneEnd=script.indexOf('\nasync function saveCalibration(',zoneStart);
  let release;
  const zoneState={current:{...gate},settingsDirty:true,loadedSettings:true,
    draftGate:()=>({...zoneState.current}),validGate:()=>true,
    send:()=>new Promise(resolve=>{release=resolve;}),calibrationPending:()=>{},
    $:()=>({textContent:''})};
  vm.runInNewContext(script.slice(zoneStart,zoneEnd),zoneState);
  const saving=zoneState.saveZone();
  zoneState.current.line=.6;
  release(true);
  await saving;
  assert.equal(zoneState.settingsDirty,true);
  const scoreStart=script.indexOf('async function saveScore(');
  const scoreEnd=script.indexOf('\nfunction calibrationPending(',scoreStart);
  const scoreState={current:.5,scoreDirty:true,number:()=>scoreState.current,
    send:()=>new Promise(resolve=>{release=resolve;}),calibrationPending:()=>{}};
  vm.runInNewContext(script.slice(scoreStart,scoreEnd),scoreState);
  const savingScore=scoreState.saveScore();scoreState.current=.6;release(true);
  await savingScore;assert.equal(scoreState.scoreDirty,true);
  const active=await check('file:',true);
  assert.equal(active.redirected,'http://127.0.0.1:8765/');
  assert.equal(active.elements.dashboard.hidden,true);
  assert.equal(active.polled,false);
  assert.equal(active.streamed,false);
  const offline=await check('file:',false);
  assert.equal(offline.redirected,'');
  assert.equal(offline.elements.fileHelp.hidden,false);
  assert.match(offline.elements.fileStatus.textContent,/Choose/);
  const http=await check('http:',true);
  assert.equal(http.polled,true);
  assert.equal(http.streamed,true);
  assert.equal(http.redirected,'');
  console.log('UI syntax, gate validation, trial accounting and 3 connection-launcher scenarios passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
