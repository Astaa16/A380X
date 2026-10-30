// SPDX-License-Identifier: GPL-3.0-or-later
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'../native/avionics');
const host=fs.readFileSync(path.join(root,'a380x_xpkit_host_win.cpp'),'utf8');
const shim=host.match(/const wchar_t \*shim=L"([^"\n]+)";/)[1];
const bridge=fs.readFileSync(path.join(root,'xp-kit/bridge.js'),'utf8');
for(const index of [1,2]){
  const messages=[];
  const c=vm.createContext({URL,URLSearchParams,performance:{now:()=>100},
    location:{href:`https://a380x.invalid/pfd.html?index=${index}`,search:`?index=${index}`},
    __A380_NATIVE:true,__a380PfdSource:'isolated test fixture',addEventListener(){},
    chrome:{webview:{postMessage:value=>messages.push(JSON.parse(value))}}});
  c.window=c;vm.runInContext(shim,c);vm.runInContext(bridge,c);
  assert.equal(c.__xphfbw.view,index);c.__xphfbw.loaded(true,'loaded');
  assert.equal(messages[0].type,'a380x-loaded');assert.equal(messages[0].side,index);
  assert.equal(messages[0].ok,true);
  c.webkit.messageHandlers.a380.postMessage({type:'a380x-airport',id:3,icao:'UAAA'});
  assert.deepEqual(messages[1],{type:'a380x-airport',id:3,icao:'UAAA'});
}
console.log('Windows WebView2 bridge: exact document-created shim, original native bridge, both sides and JSON message serialization passed');
