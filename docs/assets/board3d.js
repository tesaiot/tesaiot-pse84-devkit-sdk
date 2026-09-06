/* board3d.js — the interactive 3D board tour (3D left · synced specs right).
 *
 * Text policy: this file renders NO copy of its own. The eight stop titles are
 * read from the hero photo's .board-pins list, the spec panel clones the six
 * .spec sections of ACT I, and UI micro-strings come from .b3d-str spans in
 * the section markup — so both languages and the in-place toggle keep working
 * from the page's single source of truth.
 *
 * Camera targets and pin anchors are MEASURED positions from the model
 * (RootScreen, PCB.002, the MBUS#1 socket pair, cs_slider, dot_matrix, the
 * Cube.027/028 terminal blocks). The red OPTIGA Trust M module is absent from
 * the source model and is drawn procedurally onto MBUS#1.
 *
 * 3D model © TESA.
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { DRACOLoader } from 'three/addons/loaders/DRACOLoader.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

const SECTION = document.getElementById('board3d');
if (SECTION) init(SECTION);

function init(section){
  /* stop table: which spec section (index in .specs), which rows to highlight,
     and the measured camera pose + pin anchor */
  const STOPS = [
    {spec:2, hl:[0],   th:8,    ph:44, r:2.0,  tg:[0.34,0.16,-0.27],  hs:[0.34,0.27,-0.27]},
    {spec:0, hl:[],    th:-55,  ph:36, r:1.15, tg:[-0.72,0.09,-0.21], hs:[-0.72,0.12,-0.21]},
    {spec:1, hl:[],    th:-32,  ph:26, r:0.9,  tg:[-0.63,0.09,-0.18], hs:[-0.56,0.1,-0.14]},
    {spec:4, hl:[0,1], th:-28,  ph:52, r:1.5,  tg:[-0.5,0.03,0.4],    hs:[-0.75,0.09,0.52]},
    {spec:4, hl:[2],   th:32,   ph:56, r:1.3,  tg:[0.42,0.05,0.72],   hs:[0.43,0.13,0.89]},
    {spec:4, hl:[3],   th:70,   ph:44, r:1.5,  tg:[0.85,0.04,0.1],    hs:[0.73,0.06,0.58]},
    {spec:4, hl:[4],   th:44,   ph:28, r:1.1,  tg:[0.59,0.07,0.30],   hs:[0.59,0.15,0.30]},
    {spec:5, hl:[],    th:12,   ph:30, r:0.95, tg:[-0.42,0.08,-0.55], hs:[-0.42,0.15,-0.6]},
  ];
  const NUM = STOPS.length;
  const INTRO = {th:-25, ph:55, r:3.05, tg:new THREE.Vector3(0,0.05,0)};
  const D2R = Math.PI/180;
  const prefersStill = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const coarse = matchMedia('(pointer: coarse)').matches;

  const S = (k)=>{ const el = section.querySelector('.b3d-str[data-k="'+k+'"]');
    return el ? el.textContent.trim() : k; };
  const titles = ()=>[...document.querySelectorAll('.board-pins li span')].map(s=>s.textContent.trim());
  const specSections = ()=>[...document.querySelectorAll('#hardware .specs .spec')];

  const stage = section.querySelector('.b3d-stage');
  const pbody = section.querySelector('.b3d-pbody');
  const capEl = section.querySelector('.b3d-cap');
  const capNo = section.querySelector('.b3d-capno');
  const capTitle = section.querySelector('.b3d-captitle');
  const playBtn = section.querySelector('.b3d-play');
  const dotsEl = section.querySelector('.b3d-dots');
  const introEl = section.querySelector('.b3d-intro');
  const hintEl = section.querySelector('.b3d-loadhint');

  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({antialias:true, alpha:true});
  } catch (e) { section.hidden = true; return; }
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  renderer.toneMapping = THREE.NeutralToneMapping;
  renderer.toneMappingExposure = 1.0;
  renderer.domElement.className = 'b3d-canvas';
  stage.prepend(renderer.domElement);

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(38, 1, 0.02, 60);
  const pmrem = new THREE.PMREMGenerator(renderer);
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  scene.environmentIntensity = 0.85;

  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true; controls.dampingFactor = 0.08;
  controls.minDistance = 0.3; controls.maxDistance = 6;

  /* pins */
  const pins = STOPS.map((st,i)=>{
    const p = document.createElement('div'); p.className = 'b3d-pin';
    p.innerHTML = '<button class="b3d-dot" type="button">'+(i+1)+'</button><div class="b3d-lbl"></div>';
    p.querySelector('.b3d-dot').addEventListener('click', ()=>{ pause(); goStop(i); });
    stage.appendChild(p); return p;
  });

  const pose = {th:INTRO.th, ph:INTRO.ph, r:INTRO.r, tg:INTRO.tg.clone()};
  let goal = null, cur = -1;
  function applyPose(p){
    controls.target.copy(p.tg);
    camera.position.setFromSpherical(new THREE.Spherical(p.r, p.ph*D2R, p.th*D2R)).add(p.tg);
    camera.lookAt(p.tg);
  }
  applyPose(pose);
  controls.addEventListener('start', ()=>{ goal = null; pause(); });

  /* the red OPTIGA Trust M module, seated flat on MBUS#1 */
  function buildOptiga(){
    const g = new THREE.Group();
    const red  = new THREE.MeshStandardMaterial({color:0x9e1420, roughness:0.62, metalness:0.05});
    const dark = new THREE.MeshStandardMaterial({color:0x14161a, roughness:0.55});
    const gold = new THREE.MeshStandardMaterial({color:0xc9a24a, roughness:0.35, metalness:0.85});
    const cv = document.createElement('canvas'); cv.width=640; cv.height=512;
    const cx = cv.getContext('2d');
    cx.fillStyle='#9e1420'; cx.fillRect(0,0,640,512);
    cx.strokeStyle='rgba(255,255,255,.85)'; cx.lineWidth=6; cx.strokeRect(16,16,608,480);
    cx.fillStyle='#fff'; cx.font='600 62px Kanit, sans-serif';
    cx.fillText('OPTIGA™', 52, 150); cx.fillText('Trust M', 52, 224);
    cx.font='34px Sarabun, sans-serif'; cx.fillStyle='rgba(255,255,255,.88)';
    cx.fillText('(infineon', 52, 440); cx.fillText('Shield2Go', 380, 440);
    cx.strokeStyle='rgba(255,255,255,.2)'; cx.lineWidth=4;
    for (let i=0;i<6;i++){ cx.beginPath(); cx.moveTo(430, 90+i*28); cx.lineTo(590, 90+i*28); cx.stroke(); }
    cx.fillStyle='rgba(255,255,255,.6)';
    for (let i=0;i<8;i++){ cx.fillRect(30, 60+i*50, 14, 26); cx.fillRect(596, 60+i*50, 14, 26); }
    const tex = new THREE.CanvasTexture(cv); tex.colorSpace = THREE.SRGBColorSpace;
    const top = new THREE.MeshStandardMaterial({map:tex, roughness:0.62, metalness:0.05});
    const board = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.011, 0.27), [red,red,top,red,red,red]);
    board.position.y = 0.055; g.add(board);
    const chip = new THREE.Mesh(new THREE.BoxGeometry(0.05, 0.01, 0.05), dark);
    chip.position.set(0.05, 0.065, 0.02); g.add(chip);
    [-0.1305, 0.1305].forEach(x=>{
      const hdr = new THREE.Mesh(new THREE.BoxGeometry(0.028, 0.045, 0.235), dark);
      hdr.position.set(x, 0.027, 0); g.add(hdr);
      for (let i=0;i<8;i++){
        const pin = new THREE.Mesh(new THREE.BoxGeometry(0.006, 0.02, 0.006), gold);
        pin.position.set(x, 0.058, -0.102 + i*0.029); g.add(pin);
      }
    });
    g.position.set(-0.4215, 0.073, -0.5975);
    return g;
  }

  const draco = new DRACOLoader();
  draco.setDecoderPath('assets/draco/');
  const loader = new GLTFLoader();
  loader.setDRACOLoader(draco);
  loader.load('assets/dev-kit.glb', (gltf)=>{
    gltf.scene.traverse(o=>{
      if (o.isMesh && o.material){
        (Array.isArray(o.material)?o.material:[o.material]).forEach(m=>{
          /* the acrylic matrix cover ships as transmission glass and shimmers
             against the LEDs beneath — flatten to plain transparency */
          if (m.transmission && m.transmission > 0){
            m.transmission = 0; m.transparent = true;
            m.opacity = 0.3; m.depthWrite = false; m.side = THREE.FrontSide;
            m.needsUpdate = true;
          }
        });
      }
    });
    scene.add(gltf.scene);
    scene.add(buildOptiga());
    hintEl.classList.add('b3d-gone');
    section.classList.add('b3d-live');
    document.documentElement.classList.add('b3d-on');
    if (!coarse && !prefersStill) autoTimer = setTimeout(start, 2800);
  }, undefined, ()=>{ section.hidden = true; });

  /* render loop */
  const v3 = new THREE.Vector3();
  let last = performance.now();
  function tick(now){
    requestAnimationFrame(tick);
    const dt = Math.min((now-last)/1000, .05); last = now;
    if (goal){
      const k = 1 - Math.exp(-dt*3.2);
      let dth = goal.th - pose.th; if (dth>180) dth-=360; if (dth<-180) dth+=360;
      pose.th += dth*k; pose.ph += (goal.ph-pose.ph)*k; pose.r += (goal.r-pose.r)*k;
      pose.tg.lerp(goal.tg, k);
      applyPose(pose);
      if (Math.abs(dth)<.15 && Math.abs(goal.ph-pose.ph)<.15 && Math.abs(goal.r-pose.r)<.004) goal=null;
    } else {
      controls.update();
      const sph = new THREE.Spherical().setFromVector3(v3.copy(camera.position).sub(controls.target));
      pose.th = sph.theta/D2R; pose.ph = sph.phi/D2R; pose.r = sph.radius; pose.tg.copy(controls.target);
    }
    const w = stage.clientWidth, h = stage.clientHeight;
    pins.forEach((p,i)=>{
      v3.set(...STOPS[i].hs).project(camera);
      if (v3.z < 1){
        const x=(v3.x*.5+.5)*w, y=(-v3.y*.5+.5)*h;
        p.style.left=x+'px'; p.style.top=y+'px'; p.style.visibility='visible';
        if (x > w-160) p.setAttribute('data-flip',''); else p.removeAttribute('data-flip');
      } else p.style.visibility='hidden';
    });
    renderer.render(scene, camera);
  }
  requestAnimationFrame(tick);

  function fit(){
    const w = stage.clientWidth||1, h = stage.clientHeight||1;
    renderer.setSize(w, h, false);
    camera.aspect = w/h; camera.updateProjectionMatrix();
  }
  new ResizeObserver(fit).observe(stage); fit();

  /* ---- synced spec panel: clone the live spec sections ---- */
  function renderIntroPanel(){
    pbody.classList.add('b3d-swap');
    setTimeout(()=>{
      pbody.innerHTML = '';
      const h = document.createElement('h3'); h.textContent = S('idx-title');
      pbody.appendChild(h);
      const ul = document.createElement('ul'); ul.className = 'b3d-idx';
      titles().forEach((t,i)=>{
        const li = document.createElement('li');
        const b = document.createElement('button'); b.type = 'button';
        b.innerHTML = '<b>0'+(i+1)+'</b> ';
        b.appendChild(document.createTextNode(t));
        b.addEventListener('click', ()=>{ pause(); goStop(i); });
        li.appendChild(b); ul.appendChild(li);
      });
      pbody.appendChild(ul);
      pbody.classList.remove('b3d-swap');
    }, 300);
  }
  function renderPanel(i){
    const st = STOPS[i];
    const src = specSections()[st.spec];
    pbody.classList.add('b3d-swap');
    setTimeout(()=>{
      pbody.innerHTML = '';
      const no = document.createElement('div'); no.className = 'b3d-pnum';
      no.textContent = '0'+(i+1)+' / 0'+NUM;
      pbody.appendChild(no);
      if (src){
        const clone = src.cloneNode(true);
        /* the originals carry the page's reveal-on-scroll classes; a clone in
           this panel is never observed, so it must not wait for one */
        [clone, ...clone.querySelectorAll('.reveal')].forEach(el=>{
          el.classList.remove('reveal'); el.classList.add('is-in');
          el.style.transitionDelay = '';
        });
        clone.querySelectorAll('.spec-rows > div').forEach((row,j)=>{
          if (st.hl.includes(j)) row.classList.add('b3d-hl');
        });
        pbody.appendChild(clone);
      }
      pbody.classList.remove('b3d-swap');
    }, 300);
  }
  renderIntroPanel();

  function goStop(i){
    cur = (i+NUM)%NUM; const st = STOPS[cur];
    goal = {th:st.th, ph:st.ph, r:st.r, tg:new THREE.Vector3(...st.tg)};
    introEl.classList.add('b3d-gone');
    const t = titles();
    pins.forEach((p,j)=>{
      p.classList.toggle('b3d-act', j===cur);
      p.querySelector('.b3d-lbl').textContent = t[j] || '';
    });
    capEl.hidden = false; capEl.classList.add('b3d-swap');
    setTimeout(()=>{
      capNo.textContent = '0'+(cur+1)+' / 0'+NUM;
      capTitle.textContent = t[cur] || '';
      capEl.classList.remove('b3d-swap');
    }, 280);
    renderPanel(cur);
    dotsEl.querySelectorAll('button').forEach((d,j)=>{
      d.classList.toggle('b3d-done', j<cur); d.classList.remove('b3d-now');
      if (j===cur){ const f=d.firstElementChild; f.style.transition='none'; f.style.width='0';
        requestAnimationFrame(()=>requestAnimationFrame(()=>{ f.style.transition=''; d.classList.add('b3d-now'); })); }
    });
  }

  /* reel */
  for (let i=0;i<NUM;i++){
    const d = document.createElement('button'); d.type='button';
    d.innerHTML = '<span class="b3d-fill"></span>';
    d.addEventListener('click', ()=>{ pause(); goStop(i); });
    dotsEl.appendChild(d);
  }
  let timer = null, playing = false, autoTimer = null;
  function start(){ if (playing) return; playing = true;
    playBtn.textContent = S('pause');
    goStop(cur < 0 ? 0 : cur+1);
    timer = setInterval(()=>goStop(cur+1), 5200); }
  function pause(){ clearTimeout(autoTimer);
    if (!playing) return; playing = false; clearInterval(timer);
    playBtn.textContent = S('resume');
    dotsEl.querySelectorAll('button').forEach(d=>d.classList.remove('b3d-now')); }
  playBtn.addEventListener('click', ()=>{ playing ? pause() : start(); });
  stage.addEventListener('pointerdown', e=>{
    clearTimeout(autoTimer);
    if (playing && !e.target.closest('.b3d-deck') && !e.target.closest('.b3d-pin')) pause(); });
  /* reading intent on the panel pauses the tour */
  section.querySelector('.b3d-panel').addEventListener('pointerdown', pause);
  section.querySelector('.b3d-panel').addEventListener('wheel', pause, {passive:true});
}
