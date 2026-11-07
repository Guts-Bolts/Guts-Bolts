import { RemoteControlObject } from './remote-control.js';

export { RemoteControlObject };

const PAGE = `<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>G&B Remote Staging</title>
<style>
body{font:16px system-ui,sans-serif;max-width:720px;margin:36px auto;padding:0 18px;background:#101318;color:#e8edf2}
h1{font-size:1.5rem}p{color:#aab4c0}.panel{border:1px solid #39424d;border-radius:10px;padding:16px;margin:16px 0}
label{display:block;margin:10px 0 5px}input{box-sizing:border-box;width:100%;padding:10px;background:#1b222b;color:white;border:1px solid #56616e;border-radius:6px}
button{padding:9px 12px;margin:5px 5px 5px 0;border:1px solid #596575;border-radius:6px;background:#252f3b;color:white;cursor:pointer}
button.danger{border-color:#a64c4c}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#090c10;padding:12px;border-radius:6px;min-height:50px}
small{color:#e5bd75}
</style>
<h1>Guts &amp; Bolts Remote Control — Staging</h1>
<p>Isolated test relay. This is not the production dashboard. Do not use a production device credential here.</p>
<div class="panel">
<label for="token">Staging control token</label><input id="token" type="password" autocomplete="off" spellcheck="false">
<label for="code">One-time pairing code from the Windows agent</label><input id="code" inputmode="text" autocomplete="off" spellcheck="false">
<button id="pair">Pair this test PC</button>
</div>
<div class="panel">
<button data-action="status">Refresh status</button>
<button data-command="start">Start</button><button data-command="stop" class="danger">Stop</button><button data-command="restart">Restart</button>
<label for="priority">Process priority</label><select id="priority"><option value="Normal">Normal</option><option value="AboveNormal">Above normal</option><option value="High">High</option></select><button id="apply-priority">Apply priority</button>
<button data-command="set-auto-restart" data-args='{"enabled":true}'>Enable auto-restart</button>
<button data-command="set-auto-restart" data-args='{"enabled":false}'>Disable auto-restart</button>
<button data-action="revoke" class="danger">Revoke test device</button>
<pre id="output">Not connected.</pre>
</div>
<script>
const $=id=>document.getElementById(id), out=$('output');
async function api(action){
 const token=$('token').value.trim();
 if(!token){out.textContent='Enter the staging control token.';return}
 const r=await fetch('/control',{method:'POST',headers:{'content-type':'application/json','authorization':'Bearer '+token},body:JSON.stringify({action}),cache:'no-store'});
 const data=await r.json().catch(()=>({ok:false,error:'Invalid response'}));
 out.textContent=JSON.stringify(data,null,2);return data;
}
$('pair').onclick=()=>api({action:'pair',code:$('code').value.trim()});
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>api({action:b.dataset.action}));
document.querySelectorAll('[data-command]').forEach(b=>b.onclick=()=>api({action:'command',command:b.dataset.command,args:b.dataset.args?JSON.parse(b.dataset.args):{}}));
$('apply-priority').onclick=()=>api({action:'command',command:'set-priority',args:{priority:$('priority').value}});
</script></html>`;

function json(value,status=200){return new Response(JSON.stringify(value),{status,headers:{'content-type':'application/json','cache-control':'no-store'}})}
async function sameSecret(a,b){
 if(typeof a!=='string'||typeof b!=='string'||a.length!==b.length)return false;
 const [x,y]=await Promise.all([crypto.subtle.digest('SHA-256',new TextEncoder().encode(a)),crypto.subtle.digest('SHA-256',new TextEncoder().encode(b))]);
 const left=new Uint8Array(x),right=new Uint8Array(y);let diff=0;
 for(let i=0;i<left.length;i++)diff|=left[i]^right[i];
 return diff===0;
}
export default {
 async fetch(request,env){
  const url=new URL(request.url);
  if(url.pathname==='/healthz')return json({ok:true,service:'remote-staging'});
  if(url.pathname==='/ws/remote')return env.REMOTE_CONTROL.getByName('home').fetch(request);
  if(url.pathname==='/control'){
   if(request.method!=='POST')return json({ok:false,error:'Use POST.'},405);
   if(request.headers.get('origin')!==url.origin)return json({ok:false,error:'Same-origin requests only.'},403);
   if(request.headers.get('content-type')?.split(';')[0]!=='application/json')return json({ok:false,error:'Expected JSON.'},415);
   const auth=request.headers.get('authorization')||'';
   const match=/^Bearer ([a-f0-9]{64})$/i.exec(auth);
   if(!match||!await sameSecret(match[1],env.STAGING_CONTROL_TOKEN||''))return json({ok:false,error:'Invalid staging control token.'},401);
   const text=await request.text();if(text.length>8192)return json({ok:false,error:'Request too large.'},413);
   let body;try{body=JSON.parse(text)}catch{return json({ok:false,error:'Invalid JSON.'},400)}
   if(!body||typeof body!=='object'||Array.isArray(body)||Object.keys(body).length!==1||!body.action||typeof body.action!=='object'||Array.isArray(body.action))
    return json({ok:false,error:'Invalid control request.'},400);
   try{
    const stub=env.REMOTE_CONTROL.getByName('home');
    const result=await stub.control(env.OFFICIAL,body.action,request.headers.get('cf-connecting-ip')||'unknown');
    return json(result.body,result.status);
   }catch(error){return json({ok:false,error:'Staging relay unavailable.'},503)}
  }
  if(url.pathname==='/')return new Response(PAGE,{headers:{
   'content-type':'text/html; charset=utf-8','cache-control':'no-store',
   'content-security-policy':"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
   'referrer-policy':'no-referrer','x-content-type-options':'nosniff','x-frame-options':'DENY'
  }});
  return new Response('Not found.',{status:404,headers:{'cache-control':'no-store'}});
 }
};
