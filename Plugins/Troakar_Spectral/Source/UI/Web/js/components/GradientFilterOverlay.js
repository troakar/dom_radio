// TROAKAR SPECTRAL - Gradient Filter Overlay Header Bar
// Port of GradientFilterOverlay.h
(function (root, factory) {
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = factory();
    } else {
        root.GradientFilterOverlay = factory();
    }
})(typeof window !== 'undefined' ? window : this, function () {

    return class GradientFilterOverlay {
        constructor(containerId, gradientManager) {
            this.container  = document.getElementById(containerId);
            this.manager    = gradientManager;

            this.onSelectionChanged = null;
            this.onPointDeleted     = null;

            this.boundClick = this.handleClick.bind(this);
            this.boundContextMenu = this.handleContextMenu.bind(this);

            this.init();
        }

        init() {
            if (!this.container) return;

            this.container.addEventListener('click', this.boundClick);
            this.container.addEventListener('contextmenu', this.boundContextMenu);

            this.render();
        }

        handleClick(e) {
            var badge = e.target.closest('.gradient-badge');
            if (!badge) return;

            var pointId = parseInt(badge.dataset.pointId, 10);

            if (pointId < 0) {
                this.manager.clearActive();
            } else {
                this.manager.setActivePoint(pointId);
            }

            this.render();
            if (this.onSelectionChanged) this.onSelectionChanged();
        }

        handleContextMenu(e) {
            var badge = e.target.closest('.gradient-badge');
            if (!badge) return;
            e.preventDefault();

            var pointId = parseInt(badge.dataset.pointId, 10);
            if (pointId < 0) return;

            this.manager.removePoint(pointId);
            this.render();
            if (this.onPointDeleted) this.onPointDeleted(pointId);
            if (this.onSelectionChanged) this.onSelectionChanged();
        }

        render() {
            if (!this.container) return;
            this.container.innerHTML = '';

            var numPoints         = this.manager.points.length;
            var isGlobalSelected  = !this.manager.hasActivePoint();
            var totalWidth        = numPoints > 0 ? (100 - 10) / (numPoints + 1) : (100 - 10);

            // GLOBAL Badge
            var globalBadge      = document.createElement('div');
            globalBadge.className = 'gradient-badge'
                + (isGlobalSelected ? ' active global' : '');
            globalBadge.dataset.pointId = '-1';
            globalBadge.innerHTML =
                '<span class="badge-dot" style="background:#d4a446;"></span>'
                + '<span>GLOBAL</span>';
            this.container.appendChild(globalBadge);

            // G1..G4 Badges
            this.manager.points.forEach(function (pt) {
                var badge      = document.createElement('div');
                badge.className = 'gradient-badge'
                    + (pt.isSelected ? ' active' : '');
                badge.dataset.pointId = String(pt.id);
                badge.style.setProperty('--badge-color', pt.color);
                badge.innerHTML =
                    '<span class="badge-dot" style="background:' + pt.color + ';"></span>'
                    + '<span>' + pt.name + '</span>';

                this.container.appendChild(badge);
            }.bind(this));
        }
    };
});
