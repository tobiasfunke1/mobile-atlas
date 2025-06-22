document.addEventListener("DOMContentLoaded", function () {
  // Get the SVG element - adjust the selector as needed
  const svg = document.querySelector("svg.zoomable");
  if (!svg) return;

  // Set initial values
  let viewBox = svg.viewBox.baseVal;
  const originalViewBox = {
    x: viewBox.x,
    y: viewBox.y,
    width: viewBox.width,
    height: viewBox.height,
  };

  // State variables
  let isPanning = false;
  let wasPanning = false;
  let startPoint = { x: 0, y: 0 };
  let viewBoxStart = { x: 0, y: 0 };
  let lastTouchDistance = 0;

  // Min and max zoom levels
  const minZoom = 0.9;
  const maxZoom = 50;

  // Get SVG point from mouse/touch event
  function getPoint(evt) {
    const x = evt.clientX || evt.touches[0].clientX;
    const y = evt.clientY || evt.touches[0].clientY;
    const bb = svg.getClientRects()[0];
    return {
      x: x - bb.x,
      y: y - bb.y,
    };
  }

  // Handle zoom with constraints
  function zoom(scale, centerX, centerY) {
    // Calculate new dimensions
    const oldWidth = viewBox.width;
    const oldHeight = viewBox.height;
    const newWidth = oldWidth / scale;
    const newHeight = oldHeight / scale;

    // Check zoom constraints
    const originalRatio = originalViewBox.width / newWidth;
    if (originalRatio < minZoom || originalRatio > maxZoom) return;

    // Calculate new viewBox position to zoom toward cursor position
    const widthDiff = oldWidth - newWidth;
    const heightDiff = oldHeight - newHeight;
    const centerRatioX = centerX / svg.clientWidth;
    const centerRatioY = centerY / svg.clientHeight;

    viewBox.x += widthDiff * centerRatioX;
    viewBox.y += heightDiff * centerRatioY;
    viewBox.width = newWidth;
    viewBox.height = newHeight;

    // Ensure content remains visible
    constrainViewBox();
  }

  // Constrain viewBox to keep content visible
  function constrainViewBox() {
    // Calculate boundaries to ensure at least 10% of the SVG is always visible
    const minVisibleRatio = 0.1;
    const maxX =
      originalViewBox.x +
      originalViewBox.width -
      viewBox.width * minVisibleRatio;
    const minX = originalViewBox.x - viewBox.width * (1 - minVisibleRatio);
    const maxY =
      originalViewBox.y +
      originalViewBox.height -
      viewBox.height * minVisibleRatio;
    const minY = originalViewBox.y - viewBox.height * (1 - minVisibleRatio);

    // Apply constraints
    viewBox.x = Math.min(Math.max(viewBox.x, minX), maxX);
    viewBox.y = Math.min(Math.max(viewBox.y, minY), maxY);
  }

  // Mouse wheel event handler
  svg.addEventListener(
    "wheel",
    function (evt) {
      evt.preventDefault();

      // Calculate zoom factor based on wheel delta
      const scaleFactor = evt.deltaY < 0 ? 1.1 : 0.9;

      // Get cursor position relative to SVG
      const point = getPoint(evt);
      zoom(scaleFactor, point.x, point.y);
    },
    { passive: false },
  );

  // Prevent default action when panning
  svg.addEventListener("click", (e) => {
    if (wasPanning) {
      e.preventDefault();
      wasPanning = false;
    }
  });

  // Mouse events for panning
  svg.addEventListener("mousedown", function (evt) {
    if (evt.button === 0) {
      // Left mouse button
      evt.preventDefault();
      isPanning = true;
      startPoint = getPoint(evt);
      viewBoxStart = { x: viewBox.x, y: viewBox.y };
      svg.style.cursor = "grabbing";
    }
  });

  window.addEventListener("mousemove", function (evt) {
    if (!isPanning) return;

    wasPanning = true;

    evt.preventDefault();
    const point = getPoint(evt);

    // Calculate the distance moved in SVG coordinates
    const dx = (point.x - startPoint.x) * (viewBox.width / svg.clientWidth);
    const dy = (point.y - startPoint.y) * (viewBox.height / svg.clientHeight);

    // Update viewBox
    viewBox.x = viewBoxStart.x - dx;
    viewBox.y = viewBoxStart.y - dy;

    // Ensure content remains visible
    constrainViewBox();
  });

  window.addEventListener("mouseup", function () {
    isPanning = false;
    svg.style.cursor = "grab";
  });

  // Touch events for mobile
  svg.addEventListener("touchstart", function (evt) {
    if (evt.touches.length === 1) {
      // Single touch for panning
      evt.preventDefault();
      isPanning = true;
      startPoint = getPoint(evt);
      viewBoxStart = { x: viewBox.x, y: viewBox.y };
    } else if (evt.touches.length === 2) {
      // Two touches for pinch zooming
      evt.preventDefault();
      isPanning = false;

      // Calculate initial distance between touch points
      const touch1 = evt.touches[0];
      const touch2 = evt.touches[1];
      lastTouchDistance = Math.hypot(
        touch2.clientX - touch1.clientX,
        touch2.clientY - touch1.clientY,
      );
    }
  });

  svg.addEventListener("touchmove", function (evt) {
    evt.preventDefault();

    if (evt.touches.length === 1 && isPanning) {
      // Handle panning
      const point = getPoint(evt);

      // Calculate the distance moved in SVG coordinates
      const dx = (point.x - startPoint.x) * (viewBox.width / svg.clientWidth);
      const dy = (point.y - startPoint.y) * (viewBox.height / svg.clientHeight);

      // Update viewBox
      viewBox.x = viewBoxStart.x - dx;
      viewBox.y = viewBoxStart.y - dy;

      // Ensure content remains visible
      constrainViewBox();
    } else if (evt.touches.length === 2) {
      // Handle pinch zooming
      const touch1 = evt.touches[0];
      const touch2 = evt.touches[1];

      // Calculate current distance between touch points
      const currentTouchDistance = Math.hypot(
        touch2.clientX - touch1.clientX,
        touch2.clientY - touch1.clientY,
      );

      // Calculate scale factor
      const scaleFactor = currentTouchDistance / lastTouchDistance;
      lastTouchDistance = currentTouchDistance;

      // Calculate center point between touches
      const centerX = (touch1.clientX + touch2.clientX) / 2;
      const centerY = (touch1.clientY + touch2.clientY) / 2;

      // Apply zoom
      zoom(scaleFactor, centerX, centerY);
    }
  });

  svg.addEventListener("touchend", function () {
    isPanning = false;
  });

  // Set initial cursor style
  svg.style.cursor = "grab";
});
