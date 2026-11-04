// These fields contain owned state/help copy. Titles, author/source bylines,
// previews, IDs, sizes and callbacks keep their original data and ownership.
void ResolveLibraryCardCopy(LibraryCardView& card) {
    card.status=dkr::localisation::source(card.status);
    card.info=dkr::localisation::source(card.info);
    card.note=dkr::localisation::source(card.note);
}
