let flashVarId = 'flashvars_' + document.querySelector('#player').dataset.videoId;
console.log('Hook data', window[flashVarId]);

setTimeout(function() {
    console.log('Hook event sent');

    document.dispatchEvent(new CustomEvent('jt_ph_rook_result', {
        detail: window[flashVarId]
    }));
}, 100);