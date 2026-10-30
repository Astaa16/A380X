// SPDX-License-Identifier: GPL-3.0-or-later
(() => {
  const articles=[...document.querySelectorAll('article[data-side]')];
  const errors=document.getElementById('error');
  const queues=[[],[]];let sending=false,polling=false,revision=0,latest=null;
  function problem(error){errors.hidden=false;errors.textContent='Preview connection: '+String(error);}
  function show(data){
    if(data.revision<revision)return;revision=data.revision;latest=data;
    errors.hidden=true;
    for(let side=0;side<2;++side){
      const article=articles[side],frame=data.frames[side],view=article.querySelector('iframe').contentWindow;
      if(view.__a380Mfd){view.__a380xTrial.apply({ok:true});view.__a380Mfd.apply(frame);}
      article.querySelector('.page-name').textContent=frame.uri;
      article.querySelector('.state').textContent=!frame.powered?'Display off':frame.menu_open?'Menu open':frame.keyboard?'Editing · Enter to confirm':'Ready';
      article.querySelector('.power').checked=!!frame.powered;
    }
  }
  async function action(data){
    const response=await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});
    if(!response.ok)throw new Error('Input rejected ('+response.status+')');
    show(await response.json());
  }
  function queue(side,event){
    const list=queues[side];
    if(event.type===0&&list.length&&list[list.length-1].type===0)list[list.length-1]=event;
    else list.push(event);
    schedule();
  }
  function schedule(){if(!sending){sending=true;setTimeout(flush,25);}}
  async function flush(){
    try{
      for(let side=0;side<2;++side)if(queues[side].length)await action({action:'events',side,events:queues[side].splice(0,64)});
    }catch(error){problem(error);}finally{sending=false;if(queues.some(q=>q.length))schedule();}
  }
  function attach(article,side){
    const iframe=article.querySelector('iframe');
    new ResizeObserver(()=>{iframe.style.transform=`scale(${article.querySelector('.screen-wrap').clientWidth/768})`;}).observe(article.querySelector('.screen-wrap'));
    function connect(){
      const canvas=iframe.contentDocument.querySelector('canvas');
      if(!canvas)return problem('MFD view failed to load');
      if(canvas.dataset.previewBound)return;
      canvas.dataset.previewBound='true';
      canvas.tabIndex=0;canvas.setAttribute('aria-label',(side?'First officer':'Captain')+' MFD. Click dropdowns or fields; type and press Enter.');
      canvas.style.outlineOffset='-3px';canvas.style.touchAction='none';canvas.style.cursor='none';
      const position=(event)=>{const r=canvas.getBoundingClientRect();return {x:(event.clientX-r.left)*768/r.width,y:(event.clientY-r.top)*1024/r.height};};
      canvas.addEventListener('pointermove',event=>queue(side,{type:0,...position(event)}));
      canvas.addEventListener('pointerdown',event=>{
        if(event.button!==0)return;event.preventDefault();canvas.focus({preventScroll:true});
        canvas.setPointerCapture(event.pointerId);queue(side,{type:0,...position(event)});queue(side,{type:1});
      });
      canvas.addEventListener('pointerup',event=>{if(event.button!==0)return;queue(side,{type:0,...position(event)});queue(side,{type:2});});
      canvas.addEventListener('pointercancel',()=>{queue(side,{type:2});queue(side,{type:4});});
      canvas.addEventListener('pointerleave',event=>{if(!event.buttons)queue(side,{type:4});});
      canvas.addEventListener('wheel',event=>{event.preventDefault();queue(side,{type:0,...position(event)});queue(side,{type:3,amount:event.deltaY<0?1:-1});},{passive:false});
      canvas.addEventListener('keydown',event=>{
        if(event.ctrlKey||event.metaKey||event.altKey)return;
        const keys={Enter:'ent',Backspace:'backspace',Escape:'esc',ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right',' ':'sp','.':'dot','/':'slash','+':'plusminus','-':'plusminus'};
        const key=keys[event.key]||(/^[a-z0-9]$/i.test(event.key)?event.key.toLowerCase():null);
        if(key){event.preventDefault();queue(side,{type:5,key});}
      });
      canvas.addEventListener('paste',event=>{
        if(!latest?.frames[side].keyboard)return;
        event.preventDefault();for(const char of event.clipboardData.getData('text').slice(0,24)){
          const key=char===' '?'sp':char==='/'?'slash':char==='.'?'dot':/^[a-z0-9]$/i.test(char)?char.toLowerCase():null;
          if(key)queue(side,{type:5,key});
        }
      });
      if(latest)show(latest);
    }
    iframe.addEventListener('load',connect);
    if(iframe.contentDocument?.readyState==='complete'&&iframe.contentDocument.querySelector('canvas'))connect();
    article.querySelectorAll('[data-key]').forEach(button=>button.addEventListener('click',()=>queue(side,{type:5,key:button.dataset.key})));
    article.querySelector('.power').addEventListener('change',event=>action({action:'power',side,on:event.target.checked}).catch(problem));
  }
  articles.forEach(attach);
  document.getElementById('reset').addEventListener('click',()=>{
    queues.forEach(q=>q.splice(0));action({action:'reset',example:document.getElementById('scenario').value==='example'}).catch(problem);
  });
  async function poll(){
    if(polling||sending)return;polling=true;
    try{const response=await fetch('/api/state');if(!response.ok)throw new Error('Preview unavailable');show(await response.json());}
    catch(error){problem(error);}finally{polling=false;}
  }
  setInterval(poll,100);poll();
  window.__mfdPreview={get state(){return latest;}};
})();
