# K-FSW

@htmlonly
<div class="kfsw-hero">
  <p class="kfsw-kicker">Flight software framework</p>
  <p class="kfsw-lede">Build, test, and operate one Zephyr application on native simulation and the NUCLEO-L496ZG.</p>
  <p>K-FSW brings CSP communications, persistent parameters, LittleFS storage, and file transfer into a reproducible west workspace.</p>
  <div class="kfsw-actions">
    <a class="kfsw-button kfsw-button-primary" href="getting_started.html">Get started</a>
    <a class="kfsw-button" href="architecture.html">See the architecture</a>
    <a class="kfsw-button" href="api_reference.html">Browse the API</a>
  </div>
</div>
@endhtmlonly

## What already works

@htmlonly
<div class="kfsw-feature-grid">
  <div class="kfsw-feature"><strong>Zephyr RTOS</strong><span>One application and shell across supported targets.</span></div>
  <div class="kfsw-feature"><strong>KFSW-Linux</strong><span>Native simulation for daily development and automated tests.</span></div>
  <div class="kfsw-feature"><strong>NUCLEO-L496ZG</strong><span>Embedded builds, flashing, debugging, and physical HIL.</span></div>
  <div class="kfsw-feature"><strong>CSP + UART/KISS</strong><span>Packet routing between simulated and physical nodes.</span></div>
  <div class="kfsw-feature"><strong>Parameters</strong><span>Typed values with persistence across restarts.</span></div>
  <div class="kfsw-feature"><strong>Storage + FTP</strong><span>LittleFS storage and CSP/RDP file transfer.</span></div>
</div>
@endhtmlonly

## Try it

From a configured west workspace root, build and run KFSW-Linux:

@htmlonly
<div class="kfsw-demo-grid">
  <div class="kfsw-demo">
    <p class="kfsw-demo-label">Build and run</p>
    <pre class="kfsw-console"><code>./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run</code></pre>
  </div>
  <div class="kfsw-demo">
    <p class="kfsw-demo-label">Use the shell</p>
    <pre class="kfsw-console"><code>kfsw:~$ status
kfsw:~$ csp ping 2
kfsw:~$ param get test_u32
kfsw:~$ storage info</code></pre>
  </div>
</div>
@endhtmlonly

The prompt is `kfsw:~$`. Commands such as `status`, `csp`, `param`, and
`storage` live directly below it.

## Same application, different targets

@htmlonly
<div class="kfsw-targets">
  <div class="kfsw-target-source"><strong>K-FSW application</strong><span>Zephyr + shared K-FSW modules</span></div>
  <div class="kfsw-target-branch" aria-hidden="true"><span>runs as</span></div>
  <div class="kfsw-target-options">
    <div class="kfsw-target-card"><span class="kfsw-target-tag">Simulation</span><strong>KFSW-Linux</strong><span><code>native_sim/native/64</code></span><small>Fast local runs, software CI, and integration peers</small></div>
    <div class="kfsw-target-card"><span class="kfsw-target-tag">Hardware</span><strong>NUCLEO-L496ZG</strong><span><code>nucleo_l496zg</code></span><small>Embedded build, flash, debug, and physical HIL</small></div>
  </div>
</div>
@endhtmlonly

Hosted CI exercises simulation. Hardware validation stays an explicit local
step when a board, ST-LINK, or serial link is required.

## Explore

@htmlonly
<div class="kfsw-explore">
@endhtmlonly

- @subpage getting_started "Getting Started" — set up the workspace, run
  KFSW-Linux, or flash a board.
- @subpage architecture "Architecture" — see how the application and
  repositories fit together.
- @subpage communications "Communications" — operate CSP routing, UART/KISS,
  and RDP-backed transfers.
- @subpage services "Services" — work with logging, parameters, storage, and
  FTP.
- @subpage commands "Command Reference" — look up shell syntax and operator
  examples.
- @subpage testing "Testing" — run software checks, integration scenarios,
  and physical HIL.
- @subpage development "Development" — follow the contribution and
  multi-repository workflow.
- @subpage api_reference "API Reference" — browse public headers, data types,
  and API groups.

@htmlonly
</div>
@endhtmlonly
