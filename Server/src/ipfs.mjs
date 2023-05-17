import { webRTCStar } from '@libp2p/webrtc-star'
import wrtc from 'wrtc'
const star = webRTCStar({ wrtc })

import { webSockets } from '@libp2p/websockets'
import * as filters from '@libp2p/websockets/filters'
import { gossipsub } from '@chainsafe/libp2p-gossipsub'

export const ipfsConfig = {
    repo : './ipfs',
    config: {
        Addresses: {
            Swarm: [
                "/ip4/127.0.0.1/tcp/4003/ws",
                '/ip4/127.0.0.1/tcp/13579/ws/p2p-webrtc-star'
            ]
        },
        Bootstrap: [
            // '/ip4/127.0.0.1/tcp/4008/ws'
        ]
    },

    libp2p: {
        transports: [
            webSockets({
                filter: filters.all
            }),
            star.transport
        ],
        // pubsub: gossipsub(),
        peerDiscovery: [
            star.discovery
        ]
    },
}
