"""Renumber a checkpoint set by clicking the gates in driving order.

The game does not care where a checkpoint is. ``checkpoint_update_all``
(``objects.c``) bubble-sorts each set on ``index`` and that order *is* the
route: a racer must cross the gates in index order, and a lap counts on the
last one. So a gate added between two others only counts there once its index
does, and shifting every index after it by hand is where mistakes come from.

Nothing here guesses at the order. The author says it:

1. Pick a gate whose place in the lap is right - say index 22.
2. Click the gate that should come next. It becomes 23.
3. Keep clicking: each gate clicked becomes the next number after the last.

Gates not clicked yet keep their order among themselves and are numbered on
after the clicked ones, so the set stays one valid route at every click and the
bots' line can be watched settling into place. Gates before the anchor keep
their numbers, gaps and all. Alternate-route gates are paired with the main
gate of the same index, so each follows its main gate to its new number.

Deliberately free of ``bpy``.
"""

from __future__ import annotations

from typing import Dict, Hashable, List, Sequence, Tuple

#: ``checkpoint_update_all`` keeps the index in a ``u8``.
MAX_INDEX = 255


class ChainError(Exception):
    """A click that cannot be taken, with the reason an author can act on."""


class Chain:
    """One renumbering session over one checkpoint set.

    ``gates`` is ``(key, index)`` for every main-route gate of the set and
    ``alternates`` the same for its alternate-route gates. Nothing is changed in
    them; :meth:`indices` says what every gate's index should now be.
    """

    def __init__(self, gates: Sequence[Tuple[Hashable, int]], anchor: Hashable,
                 alternates: Sequence[Tuple[Hashable, int]] = ()):
        self.start = {key: int(index) for key, index in gates}
        if anchor not in self.start:
            raise ChainError("the gate to start from is not in this set")
        self.anchor = anchor
        self.alternates = [(key, int(index)) for key, index in alternates]
        #: The set in the order its indices give now; ties keep their order.
        self._order = [key for key, _ in sorted(gates, key=lambda kv: int(kv[1]))]
        #: The gates clicked so far, in the order they were clicked.
        self.taken: List[Hashable] = []

    @property
    def cursor(self) -> Hashable:
        """The gate the next click goes after."""
        return self.taken[-1] if self.taken else self.anchor

    @property
    def next_index(self) -> int:
        """The number the next click hands out."""
        return self.start[self.anchor] + len(self.taken) + 1

    def take(self, key: Hashable) -> Dict[Hashable, int]:
        """Make ``key`` the gate after :attr:`cursor`; every gate's new index."""
        if key not in self.start:
            raise ChainError("that gate is not in this checkpoint set")
        if key == self.anchor:
            raise ChainError("that is the gate the numbering starts from")
        if key in self.taken:
            raise ChainError("that gate is already numbered %d in this pass"
                             % self.indices()[key])
        self.taken.append(key)
        try:
            return self.indices()
        except ChainError:
            self.taken.pop()
            raise

    def back(self) -> Dict[Hashable, int]:
        """Take back the last click."""
        if self.taken:
            self.taken.pop()
        return self.indices()

    def order(self) -> List[Hashable]:
        """The whole set in driving order as it now stands.

        A gate clicked from before the anchor leaves its old place: clicking
        index 3 after 22 says it belongs after 22.
        """
        taken = set(self.taken)
        position = self._order.index(self.anchor)
        before = [k for k in self._order[:position] if k not in taken]
        after = [k for k in self._order[position + 1:] if k not in taken]
        return before + [self.anchor] + list(self.taken) + after

    def indices(self) -> Dict[Hashable, int]:
        """Every gate's index - main and alternate - for the order so far."""
        order = self.order()
        position = order.index(self.anchor)
        base = self.start[self.anchor]
        result = {key: self.start[key] for key in order[:position]}
        for offset, key in enumerate(order[position:]):
            result[key] = base + offset
        if result[order[-1]] > MAX_INDEX:
            raise ChainError(
                "numbering on from %d runs past %d, the most an index can hold; "
                "start from an earlier gate" % (base, MAX_INDEX))

        # An alternate pairs with the main gate holding its index, so it moves
        # with that gate. Read against where the session started, not the last
        # click, so taking clicks back undoes them exactly.
        old_to_new = {}
        for key, index in self.start.items():
            old_to_new.setdefault(index, result[key])
        for key, index in self.alternates:
            result[key] = old_to_new.get(index, index)
        return result

    def changed(self) -> Dict[Hashable, int]:
        """Only the gates whose index differs from where the session started."""
        start = dict(self.start)
        start.update(dict(self.alternates))
        return {k: v for k, v in self.indices().items() if start.get(k) != v}
