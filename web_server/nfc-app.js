/**
 * NFC Tab Module - Enhanced with Tabbed Interface
 */

const NFCModule = {
    initialized: false,
    currentProtocol: 'srix',
    currentAction: 'read', // read, load, settings
    currentTag: null,
    loadedFile: null,
    settings: {
        readTimeout: 10,
        writeTimeout: 10,
        waitTimeout: 5
    },

    async init() {
        if (this.initialized) return;
        console.log('[NFC] Initializing module...');

        await this.loadTabContent();
        this.setupEventListeners();
        this.loadSettings();
        this.updateNFCStatus();
        
        this.initialized = true;
        console.log('[NFC] Module initialized');
    },

    async loadTabContent() {
        const nfcTab = document.getElementById('nfc');
        if (!nfcTab) return;

        try {
            const response = await fetch('nfc-tab.html');
            if (response.ok) {
                const html = await response.text();
                nfcTab.innerHTML = html;
            } else {
                nfcTab.innerHTML = '<div class="panel"><p>Error loading NFC module</p></div>';
            }
        } catch (error) {
            console.error('[NFC] Error loading tab:', error);
            nfcTab.innerHTML = '<div class="panel"><p>Error loading NFC module</p></div>';
        }
    },

    setupEventListeners() {
        // Protocol selector
        this.disableWriteButtons();
        const protocolSelect = document.getElementById('nfc-protocol');
        if (protocolSelect) {
            protocolSelect.addEventListener('change', (e) => {
                this.onProtocolChange(e.target.value);
            });
        }

        // Action tabs
        const actionTabs = document.querySelectorAll('.nfc-tab');
        actionTabs.forEach(tab => {
            tab.addEventListener('click', () => {
                const action = tab.dataset.action;
                this.switchActionPanel(action);
            });
        });

        // READ panel buttons
        const readBtn = document.getElementById('nfc-read-btn');
        if (readBtn) {
            readBtn.addEventListener('click', () => this.handleRead());
        }

        const waitBtn = document.getElementById('nfc-wait-btn');
        if (waitBtn) {
            waitBtn.addEventListener('click', () => this.handleWait());
        }

        const saveBtn = document.getElementById('nfc-save-btn');
        if (saveBtn) {
            saveBtn.addEventListener('click', () => this.handleSave());
        }

        // LOAD panel buttons
        const browseBtn = document.getElementById('nfc-browse-btn');
        if (browseBtn) {
            browseBtn.addEventListener('click', () => this.handleBrowse());
        }

        const fullWriteBtn = document.getElementById('nfc-full-write-btn');
        if (fullWriteBtn) {
            fullWriteBtn.addEventListener('click', () => this.handleFullWrite());
        }

        const writeChangesBtn = document.getElementById('nfc-write-changes-btn');
        if (writeChangesBtn) {
            writeChangesBtn.addEventListener('click', () => this.handleWriteChanges());
        }

        // SETTINGS panel
        const settingsSaveBtn = document.getElementById('settings-save-btn');
        if (settingsSaveBtn) {
            settingsSaveBtn.addEventListener('click', () => this.saveSettings());
        }

        // Console toggle
        const consoleToggleBtn = document.getElementById('console-toggle-btn');
        const consoleContent = document.getElementById('console-content');
        if (consoleToggleBtn && consoleContent) {
            consoleToggleBtn.addEventListener('click', () => {
                const isVisible = consoleContent.style.display !== 'none';
                consoleContent.style.display = isVisible ? 'none' : 'block';
                consoleToggleBtn.textContent = isVisible ? 'SHOW CONSOLE' : 'HIDE CONSOLE';
            });
        }

        // Console clear
        const consoleClearBtn = document.getElementById('console-clear-btn');
        if (consoleClearBtn) {
            consoleClearBtn.addEventListener('click', () => this.clearConsole());
        }
    },

    switchActionPanel(action) {
        this.currentAction = action;

        // Update tab buttons
        document.querySelectorAll('.nfc-tab').forEach(tab => {
            tab.classList.remove('active');
            if (tab.dataset.action === action) {
                tab.classList.add('active');
            }
        });

        // Update panels
        document.querySelectorAll('.action-panel').forEach(panel => {
            panel.classList.remove('active');
        });

        const activePanel = document.getElementById(`panel-${action}`);
        if (activePanel) {
            activePanel.classList.add('active');
        }

        this.logConsole(`Switched to ${action.toUpperCase()} panel`, 'info');
    },

    onProtocolChange(protocol) {
        this.currentProtocol = protocol;
        this.logConsole(`Protocol changed to: ${protocol.toUpperCase()}`, 'info');
        
        // Reset all data
        this.currentTag = null;
        this.loadedFile = null;
        
        // Hide/reset UI
        this.hideTagInfo();
        this.hideFileBrowser();
        this.hideLoadedFileInfo();
        this.disableWriteButtons();
        
        // Update status
        this.updateNFCStatus();
    },

    updateNFCStatus() {
        const statusValue = document.getElementById('nfc-status-value');
        if (!statusValue) return;

        if (this.currentTag) {
            statusValue.textContent = `READY - ${this.currentProtocol.toUpperCase()}`;
            statusValue.className = 'status-value ready';
        } else if (this.loadedFile) {
            statusValue.textContent = `LOADED - ${this.currentProtocol.toUpperCase()}`;
            statusValue.className = 'status-value ready';
        } else {
            statusValue.textContent = 'IDLE';
            statusValue.className = 'status-value idle';
        }
    },

    // ==================== READ ACTIONS ====================

    async handleRead() {
        const readBtn = document.getElementById('nfc-read-btn');
        const protocol = this.currentProtocol;
        
        if (this.currentProtocol === 'auto') {
            this.logConsole('Auto-Detect: Not Already implemented', 'warning');
            alert('Auto-Detect feature is not yet implemented');
            return;
        }

        readBtn.disabled = true;
        readBtn.textContent = 'READING...';
        this.logConsole(`Reading ${this.currentProtocol.toUpperCase()} tag (timeout: ${this.settings.readTimeout}s)...`, 'info');

        try {
            let endpoint;
            if (protocol === 'srix') {
                endpoint = '/api/nfc/srix/read';
            } else if (protocol === 'mifare') {
                endpoint = '/api/nfc/mifare/read';
            } else {
                throw new Error('Unknown protocol');
            }
            
            const response = await fetch(endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ 
                    timeout: this.settings.readTimeout
                })
            });

            const data = await response.json();

            if (data.success) {
                this.currentTag = data;
                this.displayTagInfo(data);
                this.updateNFCStatus();
                this.logConsole('✅ Tag read successfully', 'success');
            } else {
                this.logConsole(`❌ ${data.message}`, 'error');
                alert('Read failed: ' + data.message);
            }
        } catch (error) {
            console.error('[NFC] Read error:', error);
            this.logConsole(`❌ Error: ${error.message}`, 'error');
            alert('Read error: ' + error.message);
        } finally {
            readBtn.disabled = false;
            readBtn.textContent = 'READ TAG';
        }
    },

    async handleWait() {
        this.logConsole('⚠️ WAIT function disabled (hardware limitation)', 'warning');
        alert('❌ Wait function is temporarily disabled due to hardware limitations.\n\n' +
            'Use "READ TAG" button instead - it has internal timeout.');
        return;
    },

    async handleSave() {
        const filename = document.getElementById('save-filename').value.trim();
        
        if (!filename) {
            alert('Please enter a filename');
            return;
        }

        if (!this.currentTag) {
            alert('No tag data to save');
            return;
        }

        this.logConsole(`Saving dump as "${filename}"...`, 'info');

        try {
            const formData = new FormData();
            formData.append('filename', filename);

            const response = await fetch('/api/nfc/save', {
                method: 'POST',
                body: formData
            });

            const data = await response.json();

            if (data.success) {
                this.logConsole(`✅ ${data.message}`, 'success');
                alert('Dump saved successfully!');
                document.getElementById('save-filename').value = '';
            } else {
                this.logConsole(`❌ ${data.message}`, 'error');
                alert('Save failed: ' + data.message);
            }
        } catch (error) {
            console.error('[NFC] Save error:', error);
            this.logConsole(`❌ Error: ${error.message}`, 'error');
            alert('Save error: ' + error.message);
        }
    },

    displayTagInfo(data) {
        const section = document.getElementById('tag-info-section');
        section.style.display = 'block';

        document.getElementById('tag-protocol').textContent = data.protocol || '--';
        document.getElementById('tag-uid').textContent = data.uid || '--';
        document.getElementById('tag-size').textContent = data.size ? `${data.size} bytes` : '--';

        // Display dump
        if (data.dump) {
            const dumpDisplay = document.getElementById('dump-display');
            let formatted = '';
            for (let i = 0; i < data.dump.length && i < 128; i += 32) {
                const line = data.dump.substr(i, 32);
                const hex = line.match(/.{1,2}/g).join(' ');
                const addr = (i / 2).toString(16).padStart(4, '0').toUpperCase();
                formatted += `${addr}: ${hex}\n`;
            }
            dumpDisplay.textContent = formatted;
        }
    },

    hideTagInfo() {
        const section = document.getElementById('tag-info-section');
        if (section) section.style.display = 'none';
    },

    async handleReadUID() {
        if (this.currentProtocol !== 'mifare') {
            this.showError('UID-only read is only for Mifare protocol');
            return;
        }
        
        const timeout = 5;
        this.showLoading('Reading UID...');
        
        try {
            const response = await fetch('/api/nfc/mifare/read-uid', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ timeout })
            });
            
            const data = await response.json();
            
            if (data.success) {
                this.showSuccess(`UID: ${data.uid}`);
                this.displayTagInfo(data);
            } else {
                this.showError(data.message || 'Read UID failed');
            }
        } catch (error) {
            this.showError('Read UID error: ' + error.message);
        }
    },


// ==================== LOAD ACTIONS ====================

    async handleBrowse() {
            const fileBrowser = document.getElementById('file-browser');
            const fileListDiv = document.getElementById('nfc-file-list');

            if (!fileListDiv) {
                console.error('[NFC] Critical: #nfc-file-list not found in DOM');
                return;
            }

            // Toggle visibility
            if (fileBrowser.style.display === 'block') {
                fileBrowser.style.display = 'none';
                return;
            }

            fileBrowser.style.display = 'block';
            
            // Reset state
            fileListDiv.innerHTML = '<div class="loading">Loading files...</div>';

            this.logConsole('Fetching file list...', 'info');

            try {
                const response = await fetch(`/api/nfc/list?protocol=${this.currentProtocol}`);
                const data = await response.json();
                
                console.log('[handleBrowse] Received data:', data);
                
                if (data.success && data.files && data.files.length > 0) {
                    // Rendering in displayFileList
                    this.displayFileList(data.files);
                    this.logConsole(`Found ${data.files.length} file(s)`, 'success');
                } else {
                    fileListDiv.innerHTML = '<div class="loading">No files found</div>';
                    this.logConsole('No files found', 'info');
                }
            } catch (error) {
                console.error('[handleBrowse] Error:', error);
                fileListDiv.innerHTML = '<div class="loading">Error loading files</div>';
                this.logConsole(`❌ Error: ${error.message}`, 'error');
            }
    },

    displayFileList(files) {
        const fileListDiv = document.getElementById('nfc-file-list');
        
        if (!fileListDiv) {
            console.error('[displayFileList] ERROR: nfc-file-list element not found!');
            return;
        }
        
        fileListDiv.innerHTML = '';
        
        files.forEach((file) => {
            const item = document.createElement('div');
            item.className = 'file-list-item';
            
            item.innerHTML = `
                <span class="file-name">${file.name}</span>
                <span class="file-ext">${file.ext}</span>
                <div class="file-actions">
                    <button class="btn-icon load-btn" title="Load">📂</button>
                    <button class="btn-icon del-btn" title="Delete">🗑️</button>
                </div>
            `;

            // Attach event listeners
            item.querySelector('.load-btn').addEventListener('click', () => {
                this.loadFile(file.fullname);
            });
            
            item.querySelector('.del-btn').addEventListener('click', (e) => {
                e.stopPropagation(); // Avoid click
                this.deleteFile(file.fullname);
            });
            
            fileListDiv.appendChild(item);
        });
        
        console.log('[displayFileList] List updated successfully');
    },

    async loadFile(filename) {
        this.logConsole(`Loading file "${filename}"...`, 'info');

        try {
            const formData = new FormData();
            formData.append('filename', filename);

            const response = await fetch('/api/nfc/load', {
                method: 'POST',
                body: formData
            });

            const data = await response.json();

            if (data.success) {
                this.loadedFile = {
                    filename: filename,
                    protocol: data.protocol,
                    uid: data.uid,
                    size: data.size
                };

                this.displayLoadedFileInfo();
                this.enableWriteButtons();
                this.hideFileBrowser();
                this.updateNFCStatus();
                this.logConsole(`✅ File loaded successfully`, 'success');
            } else {
                this.logConsole(`❌ ${data.message}`, 'error');
                alert('Load failed: ' + data.message);
            }
        } catch (error) {
            console.error('[NFC] Load error:', error);
            this.logConsole(`❌ Error: ${error.message}`, 'error');
            alert('Load error: ' + error.message);
        }
    },

    async deleteFile(filename) {
        if (!confirm(`Delete "${filename}"?`)) return;

        this.logConsole(`Deleting file "${filename}"...`, 'info');

        try {
            const response = await fetch(`/api/nfc/delete?filename=${encodeURIComponent(filename)}`, {
                method: 'DELETE'
            });

            const data = await response.json();

            if (data.success) {
                this.logConsole(`✅ File deleted`, 'success');
                this.handleBrowse(); // Refresh list
            } else {
                this.logConsole(`❌ ${data.message}`, 'error');
                alert('Delete failed: ' + data.message);
            }
        } catch (error) {
            console.error('[NFC] Delete error:', error);
            this.logConsole(`❌ Error: ${error.message}`, 'error');
        }
    },

    async handleFullWrite() {
        const protocol = this.currentProtocol;

        if (!this.loadedFile) {
            alert('No data loaded to write');
            return;
        }

        if (!confirm('⚠️ FULL WRITE will overwrite ALL tag data!\n\nAre you sure?')) {
            return;
        }

        const writeBtn = document.getElementById('nfc-full-write-btn');
        writeBtn.disabled = true;
        writeBtn.textContent = 'WRITING...';
        this.logConsole('Writing full data to tag...', 'info');

        try {
            let endpoint;
            if (protocol === 'srix') {
                endpoint = '/api/nfc/srix/write';
            } else if (protocol === 'mifare') {
                endpoint = '/api/nfc/mifare/write';
            } else {
                throw new Error('Unknown protocol');
            }
            
            const response = await fetch(endpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ 
                    timeout: this.settings.writeTimeout,
                    mode: 'full'
                })
            });

            const data = await response.json();

            if (data.success) {
                this.logConsole(`✅ ${data.message}`, 'success');
                alert('Full write successful!');
            } else {
                this.logConsole(`❌ ${data.message}`, 'error');
                alert('Write failed: ' + data.message);
            }
        } catch (error) {
            console.error('[NFC] Write error:', error);
            this.logConsole(`❌ Error: ${error.message}`, 'error');
            alert('Write error: ' + error.message);
        } finally {
            writeBtn.disabled = false;
            writeBtn.textContent = '✏️ FULL WRITE';
        }
    },

    async handleWriteChanges() {
        if (!this.loadedFile) {
            alert('No data loaded');
            return;
        }

        // Step 1: Show confirm for tag reading
        if (!confirm('⚠️ WRITE CHANGES\n\nPlace the tag on the reader and click OK to compare.\nClick CANCEL to abort.')) {
            return;
        }
        
        // Ensure protocol
        const protocol = this.currentProtocol;
        const writeBtn = document.getElementById('nfc-write-changes-btn');
        writeBtn.disabled = true;
        writeBtn.textContent = 'READING TAG...';
        this.logConsole('Reading physical tag for comparison...', 'info');

        try {
            // Step 2: Call API
            let compareEndpoint;
            if (protocol === 'srix') {
                compareEndpoint = '/api/nfc/srix/compare';
            } else if (protocol === 'mifare') {
                compareEndpoint = '/api/nfc/mifare/compare';
            } else {
                throw new Error('Unknown protocol');
            }
            
            const response = await fetch(compareEndpoint, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ timeout: this.settings.readTimeout })
            });

            const data = await response.json();

            if (!data.success) {
                this.logConsole(data.message, 'error');
                alert('❌ Comparison failed:\n' + data.message);
                writeBtn.disabled = false;
                writeBtn.textContent = 'WRITE CHANGES';
                return;
            }

            // Step 3: Check differences
            const totalDifferences = data.total_differences || 0;
            
            if (totalDifferences === 0) {
                this.logConsole('No differences found - tag already matches dump', 'success');
                alert('✅ No differences found!\n\nThe physical tag already matches the loaded dump.');
                writeBtn.disabled = false;
                writeBtn.textContent = 'WRITE CHANGES';
                return;
            }

            // Step 4: Show modal with differences
            this.logConsole(`Comparison complete: ${totalDifferences} differences found`, 'info');
            
            // Console Log
            const differences = data.differences || [];
            differences.forEach(diff => {
                const warningMsg = diff.warning ? ` [${diff.warning}]` : '';
                this.logConsole(`  Block ${diff.block}: ${diff.physical} → ${diff.loaded}${warningMsg}`, 'info');
            });

            // Show modal and wait
            const userChoice = await this.showComparisonModal(data);
            
            if (!userChoice) {
                // User cancelled
                this.logConsole('Write operation cancelled by user', 'warning');
                writeBtn.disabled = false;
                writeBtn.textContent = 'WRITE CHANGES';
                return;
            }

            // Step 5: User clicked CONTINUE - Write changes
            this.logConsole('Starting selective write...', 'info');
            writeBtn.textContent = 'WRITING...';

            // Filter blocks: remove those with warnings (Block 0, sector trailer)
            const blocksToWrite = differences
                .filter(diff => !diff.warning) //Escludi blocchi con warning
                .map(diff => diff.block);

            // Show filtered block info
            const skippedBlocks = differences.length - blocksToWrite.length;
            if (skippedBlocks > 0) {
                this.logConsole(`⚠️ Skipping ${skippedBlocks} protected blocks (UID/Sector Trailers)`, 'warning');
            }
            
            if (blocksToWrite.length === 0) {
                this.logConsole('⚠️ All differences are in protected blocks - nothing to write', 'warning');
                alert('⚠️ All differences are in protected blocks (UID or sector trailers).\n\nNothing to write.');
                writeBtn.disabled = false;
                writeBtn.textContent = 'WRITE CHANGES';
                return;
            }

            this.logConsole(`Writing ${blocksToWrite.length} blocks...`, 'info');

            // Determine selective write endpoints
            let writeEndpoint;
            if (protocol === 'srix') {
                writeEndpoint = '/api/nfc/srix/write-selective';
            } else if (protocol === 'mifare') {
                writeEndpoint = '/api/nfc/mifare/write-selective';
            } else {
                throw new Error('Unknown protocol');
            }

            try {
                const writeResponse = await fetch(writeEndpoint, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ blocks: blocksToWrite })
                });

                const writeData = await writeResponse.json();

                if (writeData.success) {
                    this.logConsole(`✅ ${writeData.message}`, 'success');
                    alert(`✅ SUCCESS!\n\n${writeData.message}\n\nBlocks written: ${writeData.blocks_count || blocksToWrite.length}`);
                } else {
                    this.logConsole(`❌ ${writeData.message}`, 'error');
                    alert(`❌ FAILED\n\n${writeData.message}`);
                }
            } catch (error) {
                this.logConsole('Error: ' + error.message, 'error');
                alert('Write error: ' + error.message);
            }

            writeBtn.disabled = false;
            writeBtn.textContent = 'WRITE CHANGES';

        } catch (error) {
            console.error('[NFC] Write Changes error:', error);
            this.logConsole('Error: ' + error.message, 'error');
            alert('Write error: ' + error.message);
            writeBtn.disabled = false;
            writeBtn.textContent = 'WRITE CHANGES';
        }
    },


    showComparisonModal(data) {
        return new Promise((resolve) => {
            const modal = document.getElementById('compare-modal');
            const closeBtn = document.getElementById('compare-modal-close');
            const cancelBtn = document.getElementById('compare-cancel-btn');
            const continueBtn = document.getElementById('compare-continue-btn');
            
            // Populate the data
            document.getElementById('compare-loaded-uid').textContent = data.loaded_uid || '--';
            document.getElementById('compare-physical-uid').textContent = data.physical_uid || '--';
            document.getElementById('compare-total-diff').textContent = 
                `${data.total_differences} / ${data.total_blocks} blocks`;
            
            // Populate block list
            const blocksList = document.getElementById('compare-blocks-list');
            blocksList.innerHTML = '';
            
            const differences = data.differences || [];
            differences.forEach(diff => {
                const item = document.createElement('div');
                item.className = 'compare-block-item';
                item.innerHTML = `
                    <span class="block-number">Block ${diff.block}</span>
                    <div class="block-data">
                        <div class="block-data-row">
                            <div class="block-data-label">Physical (Current)</div>
                            <div class="block-data-value old">${diff.physical}</div>
                        </div>
                        <div class="block-data-row">
                            <div class="block-data-label">→ Loaded (New)</div>
                            <div class="block-data-value new">${diff.loaded}</div>
                        </div>
                    </div>
                `;
                blocksList.appendChild(item);
            });
            
            // Show modal
            modal.style.display = 'flex';
            
            // Handler for closing/cancellation
            const handleCancel = () => {
                modal.style.display = 'none';
                cleanup();
                resolve(false);
            };
            
            // Handler for continue
            const handleContinue = () => {
                modal.style.display = 'none';
                cleanup();
                resolve(true);
            };
            
            // Cleanup listeners
            const cleanup = () => {
                closeBtn.removeEventListener('click', handleCancel);
                cancelBtn.removeEventListener('click', handleCancel);
                continueBtn.removeEventListener('click', handleContinue);
                modal.removeEventListener('click', handleOutsideClick);
            };
            
            // Close on outside click
            const handleOutsideClick = (e) => {
                if (e.target === modal) {
                    handleCancel();
                }
            };
            
            // Attach listeners
            closeBtn.addEventListener('click', handleCancel);
            cancelBtn.addEventListener('click', handleCancel);
            continueBtn.addEventListener('click', handleContinue);
            modal.addEventListener('click', handleOutsideClick);
        });
    },

    displayLoadedFileInfo() {
        const section = document.getElementById('loaded-file-info');
        section.style.display = 'block';

        document.getElementById('loaded-filename').textContent = this.loadedFile.filename;
        document.getElementById('loaded-protocol').textContent = this.loadedFile.protocol || '--';
        document.getElementById('loaded-uid').textContent = this.loadedFile.uid || '--';
    },

    hideLoadedFileInfo() {
        const section = document.getElementById('loaded-file-info');
        if (section) section.style.display = 'none';
    },

    hideFileBrowser() {
        const fileBrowser = document.getElementById('file-browser');
        if (fileBrowser) fileBrowser.style.display = 'none';
    },

    enableWriteButtons() {
        const fullWriteBtn = document.getElementById('nfc-full-write-btn');
        const writeChangesBtn = document.getElementById('nfc-write-changes-btn');
        
        if (fullWriteBtn) {
            fullWriteBtn.disabled = false;
            fullWriteBtn.style.opacity = '1';
            fullWriteBtn.style.cursor = 'pointer';
        }
        if (writeChangesBtn) {
            writeChangesBtn.disabled = false;
            writeChangesBtn.style.opacity = '1';
            writeChangesBtn.style.cursor = 'pointer';
        }
        
        console.log('[NFC] Write buttons enabled');
    },

    disableWriteButtons() {
        const fullWriteBtn = document.getElementById('nfc-full-write-btn');
        const writeChangesBtn = document.getElementById('nfc-write-changes-btn');
        
        if (fullWriteBtn) {
            fullWriteBtn.disabled = true;
            fullWriteBtn.style.opacity = '0.4';
            fullWriteBtn.style.cursor = 'not-allowed';
        }
        if (writeChangesBtn) {
            writeChangesBtn.disabled = true;
            writeChangesBtn.style.opacity = '0.4';
            writeChangesBtn.style.cursor = 'not-allowed';
        }
        
        console.log('[NFC] Write buttons disabled');
    },

    // ==================== SETTINGS ====================

    loadSettings() {
        const saved = localStorage.getItem('nfc_settings');
        if (saved) {
            this.settings = JSON.parse(saved);
        }

        const readTimeoutInput = document.getElementById('setting-read-timeout');
        const writeTimeoutInput = document.getElementById('setting-write-timeout');
        const waitTimeoutInput = document.getElementById('setting-wait-timeout');

        if (readTimeoutInput) readTimeoutInput.value = this.settings.readTimeout;
        if (writeTimeoutInput) writeTimeoutInput.value = this.settings.writeTimeout;
        if (waitTimeoutInput) waitTimeoutInput.value = this.settings.waitTimeout;
    },

    saveSettings() {
        this.settings.readTimeout = parseInt(document.getElementById('setting-read-timeout').value);
        this.settings.writeTimeout = parseInt(document.getElementById('setting-write-timeout').value);
        this.settings.waitTimeout = parseInt(document.getElementById('setting-wait-timeout').value);

        localStorage.setItem('nfc_settings', JSON.stringify(this.settings));
        this.logConsole('✅ Settings saved', 'success');
        alert('Settings saved!');
    },

    // ==================== CONSOLE ====================

    logConsole(message, type = 'info') {
        const consoleDiv = document.getElementById('console-content');
        if (!consoleDiv) return;

        const timestamp = new Date().toLocaleTimeString();
        const entry = document.createElement('div');
        entry.className = `log-entry log-${type}`;
        entry.textContent = `[${timestamp}] ${message}`;
        
        consoleDiv.appendChild(entry);
        consoleDiv.scrollTop = consoleDiv.scrollHeight;
    },

    clearConsole() {
        const consoleDiv = document.getElementById('console-content');
        if (consoleDiv) consoleDiv.innerHTML = '';
    }
};

// Auto-init when tab is clicked
document.addEventListener('DOMContentLoaded', () => {
    const nfcTab = document.querySelector('[data-tab="nfc"]');
    if (nfcTab) {
        nfcTab.addEventListener('click', () => {
            if (!NFCModule.initialized) {
                NFCModule.init();
            }
        });
    }
});
