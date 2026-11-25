(function () {
  'use strict';

  const modStatuses = {
    pending:'Waiting for review',review:'Under review',changes:'Changes requested',
    approved:'Approved for publication',rejected:'Rejected',published:'Published',
  };
  const runStatuses = {
    queued:'Waiting for verification',verified:'Ready for staff review',rejected:'Rejected',published:'Published',
  };
  const timestamp = new Intl.DateTimeFormat(undefined,{dateStyle:'medium',timeStyle:'short'});

  window.DKRNotificationsUI = { create({ root, user, esc, icon, button, read, mark, navigate, signIn, renderIcons }) {
    let inbox = null;
    let identity = '';
    let opened = false;
    let busy = false;
    let errorMessage = '';
    let generation = 0;
    let updatedAt = 0;
    let timer;

    function syncIdentity() {
      const account = user();
      const key = account ? account.id + ':' + account.role : '';
      if (key !== identity) {
        identity = key;
        ++generation;
        inbox = null;
        opened = busy = false;
        errorMessage = '';
        updatedAt = 0;
      }
    }

    function markup() {
      syncIdentity();
      const unread = inbox?.unread || 0;
      const label = 'Notifications' + (unread ? ', ' + unread + ' unread' : '');
      return '<button type="button" class="dkr-button quiet dkr-notification-bell" data-action="notifications-toggle" aria-label="' +
        label + '" aria-expanded="' + opened + '" aria-controls="dkr-notification-panel">' + icon('bell') +
        (unread ? '<span class="dkr-notification-badge" aria-hidden="true">' + (unread > 99 ? '99+' : unread) + '</span>' : '') +
        '</button>' + (opened ? panel() : '');
    }

    function panel() {
      const entries = inbox?.events || [];
      return '<section class="dkr-panel dkr-notification-panel" id="dkr-notification-panel" aria-labelledby="dkr-notification-title">' +
        '<div class="dkr-row dkr-between"><h2 id="dkr-notification-title">Notifications</h2>' +
        (inbox?.through && !busy ? button('notifications-read-all','Mark all as read','quiet small') : '') + '</div>' +
        (errorMessage ? '<p role="status">' + esc(errorMessage) + '</p>' : '') +
        (!inbox && busy ? '<p role="status">Loading notifications…</p>' : '') +
        (entries.length ? '<ul>' + entries.map(event => {
          const status = event.staff ? (event.type === 'mod' ? 'Mod ready for review' : 'Run ready for review') :
            (event.type === 'mod' ? modStatuses[event.status] : runStatuses[event.status]) || event.status;
          const date = new Date(event.createdAt * 1000);
          return '<li><button type="button" class="dkr-notification-item" data-action="notifications-item" data-id="' +
            esc(event.id) + '" data-unread="' + !event.read + '"><span>' +
            (!event.read ? '<span class="dkr-notification-dot" aria-label="Unread"></span>' : '') +
            '<strong>' + esc(event.title) + '</strong></span><span class="dkr-small">' + esc(status) +
            ' · <time datetime="' + date.toISOString() + '">' + esc(timestamp.format(date)) + '</time></span></button></li>';
        }).join('') + '</ul>' : (inbox ? '<p>You have no notifications.</p>' : '')) +
        (inbox?.nextCursor && !busy ? button('notifications-more','More notifications','quiet small') : '') + '</section>';
    }

    function render() {
      const slot = root.querySelector('[data-notification-slot]');
      if (!slot) return;
      const focused = slot.contains(document.activeElement) ? document.activeElement : null;
      const action = focused?.dataset.action;
      const id = focused?.dataset.id;
      slot.innerHTML = markup();
      renderIcons();
      if (action) [...slot.querySelectorAll('[data-action]')].find(item => item.dataset.action === action &&
        item.dataset.id === id)?.focus({preventScroll:true});
    }

    async function update(operation, append = false) {
      syncIdentity();
      if (!identity || busy) return false;
      const token = ++generation;
      busy = true;
      errorMessage = '';
      if (opened) render();
      try {
        const data = await operation();
        syncIdentity();
        if (token !== generation) return false;
        inbox = append && inbox ? {...data,events:[...inbox.events,...data.events]} : data;
        return true;
      } catch (error) {
        if (token === generation && opened) errorMessage = error.message || 'Notifications are temporarily unavailable.';
        return false;
      } finally {
        if (token === generation) {
          updatedAt = Date.now();
          busy = false;
          render();
        }
      }
    }

    function poll() {
      syncIdentity();
      if (identity && !opened && document.visibilityState !== 'hidden' && Date.now()-updatedAt > 30000)
        update(() => read());
    }

    function mounted() {
      if (!timer) timer = setInterval(poll,15000);
      poll();
    }

    function close(focus) {
      if (!opened) return;
      opened = false;
      render();
      if (focus) root.querySelector('[data-action="notifications-toggle"]')?.focus({preventScroll:true});
    }

    async function action(name, id) {
      if (!name.startsWith('notifications-')) return false;
      if (name === 'notifications-toggle') {
        if (!user()) await signIn();
        else if (opened) close(true);
        else {
          opened = true;
          render();
          await update(() => read());
        }
      }
      if (name === 'notifications-more' && inbox?.nextCursor)
        await update(() => read(inbox.nextCursor),true);
      if (name === 'notifications-read-all' && inbox?.through)
        await update(() => mark('read-all',{ through:inbox.through }));
      if (name === 'notifications-item') {
        const event = inbox?.events.find(row => row.id === Number(id));
        if (!event) return true;
        const current = identity;
        if (!event.read && !await update(() => mark('read',{id:event.id}))) return true;
        if (current === identity) {
          close(false);
          await navigate(event);
        }
      }
      return true;
    }

    document.addEventListener('pointerdown',event => {
      if (opened && !event.target.closest('[data-notification-slot]')) close(false);
    });
    document.addEventListener('keydown',event => {
      if (opened && event.key === 'Escape') {
        event.preventDefault();
        close(true);
      }
    });
    document.addEventListener('visibilitychange',poll);
    return { markup, action, mounted };
  } };
}());

