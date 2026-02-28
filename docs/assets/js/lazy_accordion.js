document.addEventListener("DOMContentLoaded", function () {
    // Find all accordion details blocks
    const details = document.querySelectorAll("details");

    details.forEach((targetDetail) => {
        targetDetail.addEventListener("toggle", () => {
            // Find the video inside this specific accordion
            const video = targetDetail.querySelector("video");
            if (!video) return;

            if (targetDetail.open) {
                // User opened it: Start playing (this triggers the download)
                video.play();
            } else {
                // User closed it: Pause to save CPU/RAM
                video.pause();
            }
        });
    });
});