// SPDX-License-Identifier: GPL-3.0-or-later
(() => {
  const frames = {L:document.querySelector('#nd-L'),R:document.querySelector('#nd-R')};
  const states = {L:{airport:'UAAA',mode:3,range:3,zoom:1,heading:55,wind:24,option:0,failed:false,power:true,brightness:1},
                  R:{airport:'UAAA',mode:4,range:0,zoom:1,heading:55,wind:24,option:0,failed:false,power:true,brightness:1}};
  const report = window.ndTrialReport = { loaded:{},ready:{},errors:[],states,aircraftModified:false };
  function send(side,action) { frames[side].contentWindow.postMessage({type:'nd-state',state:states[side],action},location.origin); }
  const observer = new ResizeObserver((entries) => { for(const entry of entries)entry.target.querySelector('iframe').style.transform=`scale(${entry.contentRect.width/768})`; });
  document.querySelectorAll('.screen').forEach((screen) => observer.observe(screen));
  window.addEventListener('message',(event) => {
    if(event.origin!==location.origin)return;
    const {type,side,text,ok}=event.data||{};
    if(!frames[side]||event.source!==frames[side].contentWindow)return;
    if(type==='nd-loaded'){report.loaded[side]=ok;send(side);}
    if(type==='nd-ready'){report.ready[side]=true;document.querySelector('#status-'+side).textContent=!states[side].power?'Power off':states[side].failed?'IR failure test':'Running · demo';}
    if(type==='nd-error'&&!report.errors.includes(text)){report.errors.push(text);document.querySelector('#errors').textContent=report.errors.join('\n');}
  });
  document.querySelectorAll('article').forEach((article) => {
    const side=article.dataset.side;
    article.querySelectorAll('[data-field]').forEach((input) => input.addEventListener('input',() => {
      states[side][input.dataset.field]=Number(input.value);
      if(input.dataset.field==='heading')document.querySelector('#hdg').textContent=String(input.value).padStart(3,'0')+'°';
      if(input.dataset.field==='wind')document.querySelector('#wind').textContent=input.value+' KT';
      send(side);
    }));
    article.querySelectorAll('[data-action]').forEach((button) => button.addEventListener('click',() => {
      const action=button.dataset.action;
      if(action==='failure'){states[side].failed=!states[side].failed;button.setAttribute('aria-pressed',states[side].failed);button.textContent=states[side].failed?'Restore IR':'Test IR failure';}
      if(action==='power'){states[side].power=!states[side].power;button.setAttribute('aria-pressed',!states[side].power);button.textContent=states[side].power?'Power off':'Power on';}
      send(side,action);
    }));
  });
  async function airportChanged() {
    const code=document.querySelector('#airport').value;
    const data=await fetch('/geometry/'+code).then((r)=>r.json());
    document.querySelector('#geometry-status').textContent=`${data.catalogue.name} · ${data.runways.map((r)=>r.name).join(', ')} · X-Plane scenery`;
    for(const side of ['L','R']){states[side].airport=code;send(side);}
    report.geometry=data;
  }
  document.querySelector('#airport').addEventListener('change',airportChanged);
  airportChanged().catch((error)=>{document.querySelector('#errors').textContent=error.message;});
})();
