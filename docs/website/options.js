/* SPDX-License-Identifier: GPL-3.0-only */
(() => {
  'use strict';
  const search = document.querySelector('#option-search');
  const group = document.querySelector('#option-group');
  const entries = [...document.querySelectorAll('[data-option]')];
  const groups = [...document.querySelectorAll('.option-group')];
  const status = document.querySelector('#option-count');
  const index = entries.map(node => ({node, text: node.textContent.toLowerCase(), group: node.closest('.option-group').dataset.group}));
  const toast = document.querySelector('#options-toast');
  let toastTimer;
  function announce(message) {
    toast.textContent = message; toast.classList.add('visible');
    clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove('visible'), 2200);
  }
  function filter() {
    const terms = search.value.trim().toLowerCase().split(/\s+/).filter(Boolean);
    let count = 0;
    index.forEach(entry => {
      const match = (!group.value || group.value === entry.group) && terms.every(term => entry.text.includes(term));
      entry.node.hidden = !match;
      if (match) count++;
    });
    groups.forEach(node => {node.hidden = ![...node.querySelectorAll('[data-option]')].some(card => !card.hidden);});
    document.querySelector('#option-empty').hidden = count !== 0;
    status.textContent = count + ' of ' + entries.length + ' option entries shown';
  }
  search.addEventListener('input', filter); group.addEventListener('change', filter);
  document.querySelector('#option-clear').addEventListener('click', () => {
    search.value = ''; group.value = ''; filter(); search.focus();
  });
  function revealHash() {
    const target = document.getElementById(decodeURIComponent(location.hash.slice(1)));
    if (!target) return;
    const entry = target.closest('[data-option]');
    if (entry) {search.value = ''; group.value = ''; filter(); entry.open = true;}
    if (target.classList.contains('option-group')) {search.value = ''; group.value = ''; filter();}
    requestAnimationFrame(() => target.scrollIntoView({block:'start'}));
  }
  addEventListener('hashchange', revealHash);
  document.querySelectorAll('pre').forEach(pre => {
    const button = document.createElement('button'); button.type = 'button';
    button.textContent = 'Copy'; button.className = 'copy-button';
    button.setAttribute('aria-label', 'Copy example');
    button.addEventListener('click', async () => {
      const text = pre.querySelector('code').textContent;
      try {
        if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(text);
        else {
          const field = document.createElement('textarea'); field.value = text;
          field.style.cssText = 'position:fixed;left:-9999px'; document.body.append(field); field.select();
          const copied = document.execCommand('copy'); field.remove(); button.focus();
          if (!copied) throw new Error('Copy unavailable');
        }
        announce('Example copied');
      } catch {announce('Select the example and copy it manually');}
    });
    pre.prepend(button); pre.classList.add('has-copy');
  });
  document.querySelectorAll('.table-wrap').forEach(node => {
    node.tabIndex = 0; node.setAttribute('role', 'region');
    node.setAttribute('aria-label', 'Options reference table; scroll horizontally if needed');
  });
  document.querySelector('.option-controls').hidden = false;
  const query = new URLSearchParams(location.search);
  search.value = query.get('q') || '';
  filter(); revealHash();
})();
