/* First-party mod library presentation. No credentials or persistent records live here. */
(function (window) {
  'use strict';
  window.DKRCommunityUI = { create(o) {
    const { root, esc, icon, button, banner, empty } = o;
    const $ = selector => root.querySelector(selector);
    const state = () => o.state(), store = () => o.store();
    const action = (name, label, style, extra) => '<button type="button" class="dkr-button ' + (style || 'quiet small')
      + '" data-community-action="' + name + '"' + (extra ? ' ' + extra : '') + '>' + label + '</button>';
    const op = () => Array.from(window.crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, '0')).join('');
    const motion = window.matchMedia('(prefers-reduced-motion: reduce)');
    const drafts = new Map(), edits = new Map(), errors = new Map(), loading = new Set();
    let identity = '', authority = '', report = null, removal = null, moderation = null, motionKey = '';
    let companionQuery = '', companionCategory = 'all', companionContext = '';
    let featured = [], featureIndex = 0, timer = null, transition = null, paused = motion.matches, hovered = false, focused = false;
    let deadline = 0, remaining = 30000, mountedHero = null;
    const key = itemId => (o.user()?.id || '') + ':' + itemId;
    function syncIdentity() {
      const user = o.user(), next = user ? String(user.id) : '', nextAuthority = user ? JSON.stringify([user.id, user.role, user.status]) : '';
      if (next !== identity || nextAuthority !== authority) {
        identity = next; authority = nextAuthority; drafts.clear(); edits.clear(); errors.clear(); loading.clear();
        report = null; removal = null; moderation = null;
      }
    }
    function renderIcons() { if (window.lucide) window.lucide.createIcons({ attrs: { width: 16, height: 16 } }); }
    function downloadBadge(item, detail) {
      const known = Number.isSafeInteger(item.downloadCount) && item.downloadCount >= 0;
      const full = known ? item.downloadCount.toLocaleString('en-GB') : 'Unavailable';
      const short = known ? new Intl.NumberFormat('en-GB', { notation: 'compact', maximumFractionDigits: 1 }).format(item.downloadCount) : '—';
      return '<span class="dkr-download-count' + (detail ? ' detail' : '') + '" title="' + esc(full)
        + ' tracked website download requests" aria-label="' + esc(full) + ' tracked downloads">' + icon('download')
        + '<span>' + esc(detail && known ? full : short) + (detail ? ' downloads' : '') + '</span></span>';
    }
    function companionChoices(draft) {
      const context = state().view + ':' + (draft.itemId || draft.operationId || draft.id || '') + ':' + (draft.targetId || '');
      if (context !== companionContext) { companionContext = context; companionQuery = ''; companionCategory = 'all'; }
      const items = (store()?.items || []).filter(item => !item.archived && item.id !== draft.targetId && item.id !== draft.itemId);
      const selected = Array.isArray(draft.related) ? draft.related : [];
      return '<fieldset class="dkr-fieldset dkr-companions"><legend>Companion downloads</legend>'
        + '<p class="dkr-small dkr-muted">Choose up to 10 published mods that work with this release, such as a matching texture pack.</p>'
        + (items.length ? '<div class="dkr-companion-tools"><label class="dkr-search">' + icon('search')
          + '<input type="search" data-companion-search aria-label="Search companion downloads" placeholder="Search companion downloads…" value="' + esc(companionQuery) + '"></label>'
          + '<select class="dkr-select" data-companion-category aria-label="Filter companion downloads by category"><option value="all">All categories</option>'
          + o.categories.filter(cat => cat.id !== 'all').map(cat => '<option value="' + cat.id + '"' + (companionCategory === cat.id ? ' selected' : '') + '>' + esc(cat.short) + '</option>').join('') + '</select></div>'
          + '<div class="dkr-companion-selected" aria-label="Selected companion downloads"></div><div class="dkr-companion-meta" aria-live="polite"><span data-companion-matches></span><span data-companion-count></span></div>'
          + '<p class="dkr-small dkr-muted" data-companion-empty hidden>No matching mods. Your selections are kept.</p><div class="dkr-companion-list">'
          + items.map(item => '<label class="dkr-check" data-companion-row data-category="' + esc(item.category) + '" data-search="'
            + esc((item.title + ' ' + item.author + ' ' + o.category(item.category).label + ' ' + (item.tags || []).join(' ')).toLocaleLowerCase()) + '">'
            + '<input type="checkbox" name="related" value="' + esc(item.id) + '"' + (selected.includes(item.id) ? ' checked' : '') + '><span>' + esc(item.title)
            + ' <small class="dkr-muted">· ' + esc(o.category(item.category).short) + '</small></span></label>').join('') + '</div>'
          + '<p class="dkr-companion-error" role="alert" hidden></p>' : '<p class="dkr-small dkr-muted">Published downloads will be available to select here.</p>') + '</fieldset>';
    }
    function updateCompanions(fieldset) {
      if (!fieldset) return;
      const rows = [...fieldset.querySelectorAll('[data-companion-row]')], selected = rows.filter(row => row.querySelector('input').checked);
      if (!rows.length) return;
      const query = companionQuery.trim().toLocaleLowerCase(); let matches = 0;
      for (const row of rows) {
        row.hidden = !!((query && !row.dataset.search.includes(query)) || companionCategory !== 'all' && row.dataset.category !== companionCategory);
        if (!row.hidden) matches++;
      }
      fieldset.querySelector('[data-companion-matches]').textContent = matches + ' of ' + rows.length + ' mods';
      fieldset.querySelector('[data-companion-count]').textContent = selected.length + ' / 10 selected';
      fieldset.querySelector('[data-companion-empty]').hidden = matches !== 0;
      fieldset.querySelector('.dkr-companion-selected').innerHTML = selected.length ? selected.map(row => {
        const item = store().item(row.querySelector('input').value);
        return '<span class="dkr-companion-chip">' + esc(item?.title || row.textContent.trim()) + action('companion-remove', icon('x'), 'quiet small',
          'data-id="' + esc(row.querySelector('input').value) + '" aria-label="Remove ' + esc(item?.title || 'companion') + '"') + '</span>';
      }).join('') : '<span class="dkr-small dkr-muted">No companions selected</span>';
      renderIcons();
    }
    function featureFrame(item) {
      return o.image(item.image || item.imageUrl, item.title, 'dkr-hero-img') + '<div class="dkr-hero-shade" aria-hidden="true"></div>'
        + '<div class="dkr-hero-copy"><span class="dkr-eyebrow">Community spotlight · Featured mod</span><h1 class="dkr-heading">' + esc(item.title)
        + '</h1><p>' + esc(item.summary || 'Discover your next community favourite.') + '</p><div class="dkr-row">'
        + button('detail', 'Explore this mod ' + icon('arrow-right'), 'gold', item.id) + '</div></div>';
    }
    function hero(items) {
      const next = items.filter(item => item.featured && !item.archived);
      const previous = featured[featureIndex]?.id;
      featured = next;
      featureIndex = Math.max(0, featured.findIndex(item => item.id === previous));
      if (!featured.length) return null;
      const item = featured[featureIndex];
      return '<section class="dkr-hero dkr-featured-hero" aria-label="Featured mods" aria-roledescription="carousel" data-featured-hero>'
        + '<div class="dkr-feature-layers"><div class="dkr-feature-frame is-current">' + featureFrame(item) + '</div></div>'
        + (featured.length > 1 ? '<div class="dkr-feature-controls"><div class="dkr-feature-dots" aria-label="Choose a featured mod">'
          + featured.map((entry, index) => '<button type="button" class="dkr-feature-dot" data-community-action="feature-select" data-index="' + index
            + '" aria-label="Show ' + esc(entry.title) + '" aria-pressed="' + (index === featureIndex) + '"></button>').join('') + '</div>'
          + '<div class="dkr-feature-buttons"><span class="dkr-small" data-feature-position>' + (featureIndex + 1) + ' / ' + featured.length + ' · Every 30s</span>'
          + action('feature-prev', icon('chevron-left'), 'quiet small', 'aria-label="Previous featured mod"')
          + action('feature-pause', icon(paused ? 'play' : 'pause'), 'quiet small', 'aria-label="' + (paused ? 'Play' : 'Pause') + ' featured mods"')
          + action('feature-next', icon('chevron-right'), 'quiet small', 'aria-label="Next featured mod"') + '</div></div>' : '') + '</section>';
    }
    function suspend() {
      if (timer !== null) { window.clearTimeout(timer); timer = null; remaining = Math.max(0, deadline - performance.now()); }
    }
    function schedule(reset) {
      suspend(); if (reset) remaining = 30000;
      if (state().view !== 'store' || featured.length < 2 || paused || hovered || focused || document.hidden) return;
      deadline = performance.now() + remaining;
      timer = window.setTimeout(() => { timer = null; remaining = 30000; selectFeature(featureIndex + 1, false); }, remaining);
    }
    function selectFeature(index, manual) {
      if (!featured.length) return;
      featureIndex = (index + featured.length) % featured.length;
      const layers = $('.dkr-feature-layers');
      if (!layers) return;
      window.clearTimeout(transition);
      for (const old of layers.querySelectorAll('.dkr-feature-frame:not(.is-current)')) old.remove();
      const old = layers.querySelector('.is-current');
      const frame = document.createElement('div'); frame.className = 'dkr-feature-frame'; frame.innerHTML = featureFrame(featured[featureIndex]);
      layers.append(frame); if (old) { old.setAttribute('aria-hidden', 'true'); old.inert = true; }
      if (motion.matches) { if (old) old.remove(); frame.classList.add('is-current'); }
      else {
        void frame.offsetWidth; frame.classList.add('is-current'); if (old) old.classList.remove('is-current');
        transition = window.setTimeout(() => { if (old) old.remove(); }, 430);
      }
      root.querySelectorAll('[data-community-action="feature-select"]').forEach(dot => dot.setAttribute('aria-pressed', String(Number(dot.dataset.index) === featureIndex)));
      const position = $('[data-feature-position]'); if (position) position.textContent = (featureIndex + 1) + ' / ' + featured.length + ' · Every 30s';
      renderIcons(); schedule(!!manual); preload();
    }
    function preload() {
      if (featured.length < 2) return;
      const probe = document.createElement('div'); probe.innerHTML = o.image(featured[(featureIndex + 1) % featured.length].image || featured[(featureIndex + 1) % featured.length].imageUrl, '');
      const source = probe.querySelector('img')?.src; if (source) { const img = new Image(); img.src = source; }
    }
    function ensureComments(itemId) {
      syncIdentity(); if (loading.has(itemId) || errors.has(itemId) || store().commentsFor(itemId)) return;
      loading.add(itemId); const owner = authority;
      store().loadComments(itemId).catch(error => { if (owner === authority) errors.set(itemId, error.message); }).finally(() => {
        if (owner !== authority) return;
        loading.delete(itemId);
        if (owner === authority && state().view === 'detail' && state().itemId === itemId && state().detailTab === 'comments') o.render();
      });
    }
    function commentForm(item) {
      const user = o.user();
      if (!user) return banner('Sign in to join the conversation. ' + action('comment-login', 'Continue with Discord', 'discord', 'data-item-id="' + esc(item.id) + '"'));
      const formKey = key(item.id), edit = edits.get(formKey), draft = edit || drafts.get(formKey) || { text: '', operationId: op() };
      if (!edit && !drafts.has(formKey)) drafts.set(formKey, draft);
      return '<form class="dkr-form dkr-comment-form" data-form="community-comment" data-item-id="' + esc(item.id) + '">'
        + '<p class="dkr-small dkr-muted">' + (edit ? 'Editing your comment' : 'Posting as ' + esc(user.displayName)) + '</p>'
        + '<label class="dkr-field"><span class="dkr-sr-only">' + (edit ? 'Edit your comment' : 'Write a comment') + '</span><textarea name="text" required maxlength="2000" placeholder="Leave a comment about this mod…">'
        + esc(draft.text) + '</textarea></label><div class="dkr-row dkr-between"><span class="dkr-small dkr-muted">No star rating needed · <span data-comment-length>' + draft.text.length + '</span> / 2,000</span>'
        + '<div class="dkr-row">' + (edit ? action('comment-cancel-edit', 'Cancel', 'quiet small', 'data-item-id="' + esc(item.id) + '"') : '')
        + '<button type="submit" class="dkr-button gold">' + icon(edit ? 'check' : 'send') + (edit ? 'Save changes' : 'Post comment') + '</button></div></div></form>';
    }
    function moderationForm(comment, reportRow) {
      const value = moderation && moderation.commentId === comment.id ? moderation.reason : '';
      return '<form class="dkr-form dkr-panel" data-form="community-moderation" data-comment-id="' + esc(comment.id) + '" data-item-id="' + esc(comment.itemId)
        + '" data-revision="' + comment.revision + '"' + (reportRow ? ' data-report-id="' + esc(reportRow.id) + '" data-report-revision="' + reportRow.revision + '"' : '') + '>'
        + '<label class="dkr-field"><span>Moderation reason</span><textarea name="reason" required maxlength="1000">' + esc(value) + '</textarea></label><div class="dkr-row">'
        + (!comment.deleted ? '<button type="submit" name="decision" value="' + (comment.hidden ? 'restore' : 'hide') + '" class="dkr-button ' + (comment.hidden ? 'gold' : 'red') + '">'
          + (comment.hidden ? 'Restore comment' : 'Hide comment') + '</button>' : '')
        + (reportRow ? '<button type="submit" name="decision" value="keep" class="dkr-button quiet">Dismiss report</button>' : '')
        + action('comment-cancel-moderation', 'Cancel') + '</div></form>';
    }
    function commentCard(comment) {
      const own = !!o.user() && comment.isOwn;
      let controls = own ? action('comment-edit', 'Edit', 'quiet small', 'data-id="' + esc(comment.id) + '"') + action('comment-delete', 'Delete', 'quiet small', 'data-id="' + esc(comment.id) + '"')
        : action('comment-report', 'Report', 'quiet small', 'data-id="' + esc(comment.id) + '"');
      if (o.isStaff()) controls += action('comment-moderate', 'Moderate', 'quiet small', 'data-id="' + esc(comment.id) + '"');
      if (removal === comment.id) controls += '<div class="dkr-banner gold"><div>Delete this comment?<div class="dkr-row dkr-spaced">'
        + action('comment-confirm-delete', 'Delete my comment', 'red small', 'data-id="' + esc(comment.id) + '"') + action('comment-cancel-delete', 'Keep comment') + '</div></div></div>';
      if (report?.id === comment.id) controls += '<form class="dkr-form dkr-panel" data-form="community-report" data-comment-id="' + esc(comment.id) + '">'
        + '<label class="dkr-field"><span>Report reason</span><select name="reason" required>' + ['Spam or advertising', 'Harassment or abusive language', 'Off-topic or misleading content'].map(reason => '<option>' + reason + '</option>').join('')
        + '</select></label><div class="dkr-row"><button type="submit" class="dkr-button quiet small">Submit report</button>' + action('comment-cancel-report', 'Cancel') + '</div></form>';
      if (moderation?.commentId === comment.id) controls += moderationForm(comment);
      return '<article class="dkr-comment"><div class="dkr-comment-avatar" aria-hidden="true">' + esc((comment.displayName || 'P').slice(0, 1).toUpperCase())
        + '</div><div class="dkr-comment-content"><div class="dkr-row"><strong>' + esc(comment.displayName) + '</strong><span class="dkr-small dkr-muted">'
        + esc(o.dateText(comment.createdAt)) + (comment.updatedAt !== comment.createdAt ? ' · edited' : '') + '</span></div><p class="dkr-safe-wrap">' + esc(comment.text)
        + '</p><div class="dkr-row">' + controls + '</div></div></article>';
    }
    function commentsPanel(item) {
      syncIdentity(); const page = store().commentsFor(item.id);
      return '<div class="dkr-sectionhead"><div><h2 class="dkr-title">Player comments</h2><p>Ask a question, share a tip or talk about this mod.</p></div><span class="dkr-small dkr-muted">Oldest first</span></div>'
        + commentForm(item) + (errors.has(item.id) ? banner(esc(errors.get(item.id)) + '<div class="dkr-spaced">' + action('comment-reload', 'Try again') + '</div>', 'error')
          : !page ? '<p class="dkr-small dkr-muted" role="status">Loading player comments…</p>'
            : '<div class="dkr-comments">' + (page.comments.length ? page.comments.map(commentCard).join('') : empty('No comments yet', 'Be the first to start the conversation.')) + '</div>'
              + (page.nextCursor ? '<div class="dkr-more">' + action('comment-more', 'Load more comments') + '</div>' : ''));
    }
    function moderationPanel() {
      syncIdentity(); const D = store();
      if (D.commentModerationError) return banner(esc(D.commentModerationError) + '<div class="dkr-spaced">' + action('comment-reload-moderation', 'Retry comment reports') + '</div>', 'error');
      const reports = D.commentReports || [], hidden = (D.hiddenComments || []).filter(comment => !reports.some(report => report.commentId === comment.id));
      return '<h2 class="dkr-title">Comment reports</h2><div class="dkr-list dkr-spaced">' + (reports.length ? reports.map(row => '<article class="dkr-panel dkr-stack"><div class="dkr-row dkr-between"><strong>'
        + esc(row.comment.itemTitle) + '</strong><span class="dkr-small dkr-muted">' + esc(row.comment.displayName) + '</span></div><p class="dkr-safe-wrap">' + esc(row.comment.text)
        + '</p><p class="dkr-small dkr-muted">Reported by ' + esc(row.reporterName) + ' · ' + esc(row.reason) + '</p>' + moderationForm(row.comment, row) + '</article>').join('')
        : empty('No comment reports', 'Reported comments will appear here for staff review.')) + '</div>'
        + '<h2 class="dkr-title dkr-spaced">Hidden comments</h2><div class="dkr-list dkr-spaced">' + (hidden.length ? hidden.map(comment => '<article class="dkr-panel dkr-stack"><strong>' + esc(comment.itemTitle)
          + ' · ' + esc(comment.displayName) + '</strong><p class="dkr-safe-wrap">' + esc(comment.text) + '</p><p class="dkr-small dkr-muted">Reason: ' + esc(comment.hiddenReason)
          + '</p>' + moderationForm(comment) + '</article>').join('') : '<p class="dkr-small dkr-muted">No hidden comments.</p>') + '</div>';
    }
    function animate(container) {
      if (!container || motion.matches) return;
      const elements = container.querySelectorAll('.dkr-hero,.dkr-categories,.dkr-sectionhead,.dkr-tools,.dkr-modcard,.dkr-pagehead,.dkr-detailgrid,.dkr-localnav,.dkr-detail-content,.dkr-comment-form,.dkr-comment,.dkr-panel,.dkr-community-band');
      const top = [...elements].filter(element => ![...elements].some(parent => parent !== element && parent.contains(element)));
      top.forEach((element, index) => { element.style.setProperty('--dkr-enter-delay', Math.min(index, 7) * 60 + 'ms'); element.classList.remove('dkr-enter'); void element.offsetWidth; element.classList.add('dkr-enter'); });
    }
    function mounted() {
      syncIdentity(); root.querySelectorAll('.dkr-companions').forEach(updateCompanions);
      const heroNode = $('[data-featured-hero]');
      if (heroNode !== mountedHero) {
        mountedHero = heroNode;
        if (heroNode) {
          hovered = heroNode.matches(':hover'); focused = heroNode.contains(document.activeElement);
          heroNode.addEventListener('mouseenter', () => { hovered = true; suspend(); });
          heroNode.addEventListener('mouseleave', () => { hovered = false; schedule(); });
          heroNode.addEventListener('focusin', () => { focused = true; suspend(); });
          heroNode.addEventListener('focusout', () => window.setTimeout(() => { focused = !!mountedHero?.contains(document.activeElement); schedule(); }, 0));
          schedule(); preload();
        } else { suspend(); remaining = 30000; hovered = false; focused = false; }
      }
      const s = state(), nextKey = [s.view, s.category, s.tag, s.itemId, s.detailTab, s.queueTab, s.accountTab, s.adminTab].join(':');
      if (!s.pageLoading && !$('.dkr-progress') && nextKey !== motionKey) { motionKey = nextKey; animate($('#dkr-community-main')); }
      if (s.view === 'detail' && s.detailTab === 'comments' && !s.pageLoading && store()) ensureComments(s.itemId);
    }
    root.addEventListener('input', event => {
      const input = event.target;
      if (input.matches('[data-companion-search]')) { companionQuery = input.value; updateCompanions(input.closest('.dkr-companions')); }
      if (input.name === 'related' && input.closest('.dkr-companions')) {
        const fieldset = input.closest('.dkr-companions'), checked = [...fieldset.querySelectorAll('input[name="related"]:checked')];
        const error = fieldset.querySelector('.dkr-companion-error');
        if (checked.length > 10) { input.checked = false; error.hidden = false; error.textContent = 'Choose up to 10 companion downloads.'; }
        else error.hidden = true;
        updateCompanions(fieldset);
      }
      const form = input.closest('form[data-form="community-comment"]');
      if (form && input.name === 'text') {
        syncIdentity(); const formKey = key(form.dataset.itemId), edit = edits.get(formKey), value = edit || drafts.get(formKey) || {};
        if (value.text !== input.value) value.operationId = op(); value.text = input.value;
        if (!edit) drafts.set(formKey, value); form.querySelector('[data-comment-length]').textContent = input.value.length;
      }
      if (input.closest('form[data-form="community-moderation"]') && input.name === 'reason') {
        const form = input.closest('form'); moderation = { commentId: form.dataset.commentId, reason: input.value };
      }
    });
    root.addEventListener('change', event => {
      if (event.target.matches('[data-companion-category]')) { companionCategory = event.target.value; updateCompanions(event.target.closest('.dkr-companions')); }
    });
    function refreshDownload(event) {
      const anchor = event.target.closest('a[data-download-release]');
      if (anchor && store()?.downloadURL) { const url = store().downloadURL(anchor.dataset.downloadRelease); if (url) anchor.href = url; }
    }
    root.addEventListener('click', refreshDownload, true); root.addEventListener('auxclick', refreshDownload, true);
    root.addEventListener('click', event => {
      const target = event.target.closest('[data-community-action]'); if (!target) return;
      event.preventDefault(); event.stopImmediatePropagation();
      runAction(target).catch(error => o.flash(error.message, 'error'));
    });
    async function runAction(target) {
      syncIdentity(); const name = target.dataset.communityAction, D = store(), s = state();
      if (name.startsWith('feature-')) {
        if (name === 'feature-pause') { paused = !paused; target.innerHTML = icon(paused ? 'play' : 'pause'); target.setAttribute('aria-label', (paused ? 'Play' : 'Pause') + ' featured mods'); renderIcons(); schedule(); }
        else selectFeature(name === 'feature-select' ? Number(target.dataset.index) : featureIndex + (name === 'feature-next' ? 1 : -1), true);
        return;
      }
      if (name === 'companion-remove') {
        const fieldset = target.closest('.dkr-companions'), input = [...fieldset.querySelectorAll('input[name="related"]')].find(input => input.value === target.dataset.id);
        if (input) { input.checked = false; input.dispatchEvent(new Event('input', { bubbles: true })); fieldset.querySelector('[data-companion-search]').focus({ preventScroll: true }); }
        return;
      }
      if (name === 'comment-login') { await o.go('login', { loginMode: 'player', returnTo: { view: 'detail', itemId: target.dataset.itemId, detailTab: 'comments' } }); return; }
      if (name === 'comment-reload' || name === 'comment-more') {
        const itemId = s.itemId, owner = authority; errors.delete(itemId); loading.add(itemId);
        try { await D.loadComments(itemId, name === 'comment-more' ? D.commentsFor(itemId)?.nextCursor : undefined); }
        catch (error) { if (owner === authority) errors.set(itemId, error.message); }
        finally { if (owner === authority) { loading.delete(itemId); if (state().itemId === itemId) o.render(); } }
        return;
      }
      if (name === 'comment-reload-moderation') { await D.loadCommentModeration(); o.render(); return; }
      if (name === 'comment-cancel-edit') { edits.delete(key(target.dataset.itemId)); o.render(); return; }
      if (name === 'comment-cancel-report') { report = null; o.render(); return; }
      if (name === 'comment-cancel-delete') { removal = null; o.render(); return; }
      if (name === 'comment-cancel-moderation') { moderation = null; o.render(); return; }
      const comment = D.commentsFor(s.itemId)?.comments.find(comment => comment.id === target.dataset.id);
      if (!comment) return;
      if (!o.user()) { await o.go('login', { loginMode: 'player', returnTo: { view: 'detail', itemId: comment.itemId, detailTab: 'comments' } }); return; }
      if (name === 'comment-edit' && comment.isOwn) { edits.set(key(comment.itemId), { ...comment, operationId: op() }); o.render(); $('[data-form="community-comment"] textarea')?.focus(); }
      if (name === 'comment-delete' && comment.isOwn) { removal = comment.id; o.render(); }
      if (name === 'comment-confirm-delete' && comment.isOwn) await o.perform(null, async () => {
        const owner = authority, result = await D.changeComment(comment.id, comment.itemId, { action: 'delete', operationId: op(), expectedRevision: comment.revision });
        if (owner !== authority) return; removal = null; edits.delete(key(comment.itemId)); o.render(); o.flash(result.refreshError || 'Your comment was deleted.', result.refreshError ? 'error' : undefined);
      });
      if (name === 'comment-report') { report = { id: comment.id }; o.render(); }
      if (name === 'comment-moderate' && o.isStaff()) { moderation = { commentId: comment.id, reason: '' }; o.render(); }
    }
    root.addEventListener('submit', event => {
      const form = event.target;
      if (!form.dataset.form?.startsWith('community-')) return;
      event.preventDefault(); event.stopImmediatePropagation();
      if (!form.reportValidity()) return;
      syncIdentity(); const user = o.user();
      if (!user) { o.flash('Sign in before sending a comment action.', 'error'); return; }
      const data = new FormData(form), itemId = form.dataset.itemId, owner = authority, decision = event.submitter?.value;
      o.perform(form, async () => {
        const D = store(); let result;
        if (form.dataset.form === 'community-comment') {
          const formKey = key(itemId), edit = edits.get(formKey), draft = edit || drafts.get(formKey);
          result = edit ? await D.changeComment(edit.id, itemId, { action: 'edit', text: data.get('text'), expectedRevision: edit.revision, operationId: edit.operationId })
            : await D.saveComment(itemId, { text: data.get('text'), operationId: draft.operationId });
          if (owner !== authority) return;
          if (edit) edits.delete(formKey); else drafts.delete(formKey);
          errors.delete(itemId); o.render(); o.flash(result.refreshError || (edit ? 'Your comment was updated.' : 'Your comment was added.'), result.refreshError ? 'error' : undefined);
        } else if (form.dataset.form === 'community-report') {
          await D.reportComment(form.dataset.commentId, { reason: data.get('reason') });
          if (owner !== authority) return; report = null; o.render(); o.flash('Report sent to the moderation team.');
        } else if (form.dataset.form === 'community-moderation') {
          result = await D.moderateComment(form.dataset.commentId, itemId, { action: decision, reason: data.get('reason'), operationId: op(), expectedRevision: Number(form.dataset.revision),
            ...(form.dataset.reportId ? { reportId: form.dataset.reportId, expectedReportRevision: Number(form.dataset.reportRevision) } : {}) });
          if (owner !== authority) return; moderation = null; o.render(); o.flash(result.refreshError || 'Comment moderation saved.', result.refreshError ? 'error' : undefined);
        }
      });
    });
    document.addEventListener('visibilitychange', () => { if (document.hidden) suspend(); else schedule(); });
    motion.addEventListener('change', () => { paused = motion.matches; suspend(); remaining = 30000; const control = $('[data-community-action="feature-pause"]'); if (control) { control.innerHTML = icon(paused ? 'play' : 'pause'); control.setAttribute('aria-label', (paused ? 'Play' : 'Pause') + ' featured mods'); renderIcons(); } schedule(); });
    window.addEventListener('pagehide', () => { suspend(); window.clearTimeout(transition); });
    return { hero, companionChoices, downloadBadge, commentsPanel, moderationPanel, mounted, animate };
  } };
}(window));
