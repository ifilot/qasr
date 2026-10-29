/* SPDX-License-Identifier: GPL-3.0-only */
(() => {
  'use strict';
  const $ = (s, root = document) => root.querySelector(s);
  const $$ = (s, root = document) => [...root.querySelectorAll(s)];
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let toastTimer;
  function toast(message) {
    const node = $('#toast'); node.textContent = message; node.classList.add('visible');
    clearTimeout(toastTimer); toastTimer = setTimeout(() => node.classList.remove('visible'), 2500);
  }
  async function copy(text) {
    try {
      if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(text);
      else {
        const field = document.createElement('textarea');
        field.value = text; field.setAttribute('readonly', '');
        field.style.cssText = 'position:fixed;left:-9999px;top:0';
        document.body.append(field); field.select();
        const done = document.execCommand('copy'); field.remove();
        if (!done) throw new Error('clipboard unavailable');
      }
      toast('Copied to clipboard');
    } catch {
      toast('Select the command text and copy it manually');
    }
  }
  $$('pre').forEach(pre => {
    const button = document.createElement('button'); button.type = 'button';
    button.className = 'copy-button'; button.textContent = 'Copy';
    button.setAttribute('aria-label', 'Copy code block');
    button.addEventListener('click', () => copy($('code', pre).textContent));
    pre.prepend(button); pre.classList.add('has-copy');
  });
  $$('.table-wrap').forEach(wrap => {
    wrap.tabIndex = 0; wrap.setAttribute('role', 'region');
    const caption = $('caption', wrap);
    wrap.setAttribute('aria-label', caption ? caption.textContent : 'Reference table; scroll horizontally if needed');
  });
  const menu = $('.menu-button'), sidebar = $('#sidebar'), backdrop = $('.nav-backdrop');
  const mobile = matchMedia('(max-width: 950px)');
  function setMenu(open) {
    sidebar.classList.toggle('open', open); menu.setAttribute('aria-expanded', String(open));
    menu.setAttribute('aria-label', open ? 'Close navigation' : 'Open navigation');
    backdrop.hidden = !open;
    document.body.style.overflow = open ? 'hidden' : '';
    sidebar.inert = mobile.matches && !open;
    if (open) $('[data-open-search]', sidebar).focus();
  }
  mobile.addEventListener('change', () => setMenu(false));
  setMenu(false);
  menu.addEventListener('click', () => setMenu(!sidebar.classList.contains('open')));
  backdrop.addEventListener('click', () => {setMenu(false); menu.focus();});
  $$('.sidebar a').forEach(a => a.addEventListener('click', () => {
    const wasOpen = sidebar.classList.contains('open'); setMenu(false);
    const target = a.hash && document.getElementById(a.hash.slice(1));
    if (wasOpen && target) {target.tabIndex = -1; target.focus({preventScroll:true});}
  }));
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape' && sidebar.classList.contains('open')) { setMenu(false); menu.focus(); }
  });
  const sections = $$('.searchable');
  const navLinks = $$('.sidebar nav a');
  if ('IntersectionObserver' in window) {
    const observer = new IntersectionObserver(entries => {
      const visible = entries.filter(e => e.isIntersecting).sort((a,b) => a.boundingClientRect.top-b.boundingClientRect.top);
      if (!visible.length) return;
      navLinks.forEach(a => {
        if (a.hash === '#'+visible[0].target.id) a.setAttribute('aria-current', 'location');
        else a.removeAttribute('aria-current');
      });
    }, {rootMargin: '-5% 0px -70% 0px', threshold: 0});
    sections.forEach(section => observer.observe(section));
  }
  function activateTab(tab) {
    const demo = tab.closest('.visual-demo');
    $$('[role=tab]', demo).forEach(button => {
      const active = button === tab; button.setAttribute('aria-selected', String(active)); button.tabIndex = active ? 0 : -1;
    });
    const img = $('figure img', demo); img.src = tab.dataset.image; img.alt = tab.dataset.caption;
    $('figcaption', demo).textContent = tab.dataset.caption;
    $('pre code', demo).textContent = tab.dataset.command;
  }
  $$('.visual-demo').forEach((demo, index) => {
    const panel = $('[role=tabpanel]', demo); panel.id = 'demo-panel-'+index;
    $$('[role=tab]', demo).forEach((tab, tabIndex) => {
      tab.id = 'demo-tab-'+index+'-'+tabIndex; tab.setAttribute('aria-controls', panel.id);
      if (tab.getAttribute('aria-selected') === 'true') panel.setAttribute('aria-labelledby', tab.id);
      tab.addEventListener('click', () => {activateTab(tab); panel.setAttribute('aria-labelledby', tab.id);});
      tab.addEventListener('keydown', e => {
        const tabs = $$('[role=tab]', demo); const current = tabs.indexOf(tab);
        let next = current;
        if (e.key === 'ArrowRight') next = (current+1)%tabs.length;
        else if (e.key === 'ArrowLeft') next = (current+tabs.length-1)%tabs.length;
        else if (e.key === 'Home') next = 0;
        else if (e.key === 'End') next = tabs.length-1;
        else return;
        e.preventDefault(); tabs[next].focus(); tabs[next].click();
      });
    });
  });
  const form = $('#recipe-form');
  function recipe() {
    const input = form.elements.input.value, style = form.elements.style.value;
    const parts = ['build-release/vasp-render', 'examples/'+input];
    if (style === 'illustrated') parts.push('--config', 'examples/illustrated.json');
    else parts.push('--style', 'shaded');
    parts.push('--view', form.elements.view.value, '--width', form.elements.size.value, '--height', form.elements.size.value,
      '--frame', form.elements.frame.value);
    const periodic = form.elements.periodic.checked;
    form.elements.colors.disabled = !periodic;
    if (periodic) parts.push('--expand-xy', '1,1', '--image-colors', form.elements.colors.value);
    if (form.elements.annotations.checked) parts.push('--labels', 'element', '--legend', 'on', '--axes', 'on', '--scale-bar', 'on');
    if (form.elements.transparent.checked) parts.push('--background', "'#ffffff00'");
    parts.push('-o', 'structure.png');
    $('#generated-command').textContent = parts.join(' ');
    $('#builder-note').textContent = input.endsWith('.xml') || periodic
      ? 'Uses development features. Confirm XML in --build-info and --expand-xy in --help.'
      : 'Run from the source checkout after extracting the example kit into examples/.';
  }
  form.addEventListener('change', recipe); form.addEventListener('submit', e => e.preventDefault()); recipe();
  const searchDialog = $('#search-dialog'), searchInput = $('#search-input'), results = $('#search-results');
  const index = sections.map(section => ({id: section.id, title: section.dataset.title,
    text: section.textContent.replace(/\s+/g, ' ').trim()}));
  function search() {
    const terms = searchInput.value.toLowerCase().trim().split(/\s+/).filter(Boolean);
    results.replaceChildren();
    if (!terms.length) {$('#search-status').textContent = 'Search runs locally. No data leaves your browser.'; return;}
    const matches = index.filter(entry => terms.every(term => (entry.title+' '+entry.text).toLowerCase().includes(term)));
    $('#search-status').textContent = matches.length ? matches.length+' matching sections' : 'No matching sections. Try a shorter term or a CLI flag.';
    matches.forEach(entry => {
      const a = document.createElement('a'); a.href = '#'+entry.id;
      const title = document.createElement('strong'); title.textContent = entry.title;
      const snippet = document.createElement('span');
      const at = entry.text.toLowerCase().indexOf(terms[0]);
      const begin = Math.max(0, at-55); snippet.textContent = (begin ? '…' : '')+entry.text.slice(begin, begin+190)+'…';
      a.append(title, snippet); results.append(a);
      a.addEventListener('click', () => {searchDialog.close(); setMenu(false);});
    });
  }
  function openSearch() {searchDialog.showModal(); searchInput.focus();}
  $$('[data-open-search]').forEach(button => button.addEventListener('click', openSearch));
  searchInput.addEventListener('input', search);
  document.addEventListener('keydown', e => {
    if (e.key === '/' && !/INPUT|TEXTAREA|SELECT/.test(document.activeElement.tagName) && !searchDialog.open && !$('#image-dialog').open) {
      e.preventDefault(); openSearch();
    }
  });
  const imageDialog = $('#image-dialog');
  $$('[data-lightbox]').forEach(button => button.addEventListener('click', () => {
    const img = $('img', imageDialog); img.src = button.dataset.lightbox; img.alt = button.dataset.title;
    $('#image-title').textContent = button.dataset.title;
    $('#image-download').href = button.dataset.lightbox; imageDialog.showModal();
  }));
  $$('[data-close-dialog]').forEach(button => button.addEventListener('click', () => button.closest('dialog').close()));
  $$('dialog').forEach(dialog => dialog.addEventListener('click', e => {
    const r = dialog.getBoundingClientRect();
    if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) dialog.close();
  }));
  $('.print-button').addEventListener('click', () => window.print());
  let detailState;
  window.addEventListener('beforeprint', () => {
    detailState = $$('details').map(node => [node, node.open]); detailState.forEach(([node]) => node.open = true);
  });
  window.addEventListener('afterprint', () => {if (detailState) detailState.forEach(([node, open]) => node.open = open);});
  if (reduced) document.documentElement.style.scrollBehavior = 'auto';
})();
