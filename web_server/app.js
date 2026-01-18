/**
 * ESP32 Control Panel - Main JavaScript
 * File Manager + Settings for LittleFS
 */

// ============================================
// Tab Navigation
// ============================================
const tabs = document.querySelectorAll('.tab');
const tabContents = document.querySelectorAll('.tab-content');
let refreshTimeout = null;

tabs.forEach(tab => {
  tab.addEventListener('click', () => {
    const targetId = tab.dataset.tab;
    tabs.forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    tabContents.forEach(content => {
      content.classList.remove('active');
      if (content.id === targetId) {
        content.classList.add('active');
      }
    });
  });
});

// ============================================
// Modal Functions
// ============================================
const modal = document.getElementById('modal');
const modalTitle = document.getElementById('modal-title');
const modalBody = document.getElementById('modal-body');
const modalClose = document.getElementById('modal-close');
const modalCancel = document.getElementById('modal-cancel');
const modalConfirm = document.getElementById('modal-confirm');
let modalCallback = null;

function openModal(title, bodyHtml, onConfirm) {
  modalTitle.textContent = title;
  modalBody.innerHTML = bodyHtml;
  modalCallback = onConfirm;
  modal.classList.add('active');
}

function closeModal() {
  modal.classList.remove('active');
  modalCallback = null;
}

modalClose.addEventListener('click', closeModal);
modalCancel.addEventListener('click', closeModal);
modalConfirm.addEventListener('click', () => {
  if (modalCallback) {
    modalCallback();
  }
  closeModal();
});

modal.addEventListener('click', (e) => {
  if (e.target === modal) {
    closeModal();
  }
});

// ============================================
// File Manager Functions
// ============================================
const btnRefresh = document.getElementById('btn-refresh');
const btnUpload = document.getElementById('btn-upload');
const fileUploadInput = document.getElementById('file-upload-input');
const btnNewFile = document.getElementById('btn-new-file');
const btnNewFolder = document.getElementById('btn-new-folder');
const fileList = document.getElementById('file-list');
const breadcrumb = document.getElementById('breadcrumb');

// Track current path
let currentPath = '/';

// Refresh file list
async function refreshFiles(force = false) {
    // Debouncing: wait 150ms before reloading
    if (!force && refreshTimeout) {
        clearTimeout(refreshTimeout);
    }
    
    if (!force) {
        refreshTimeout = setTimeout(() => refreshFiles(true), 150);
        return;
    }
    
    try {
        const response = await fetch(`/api/files?path=${encodeURIComponent(currentPath)}`);
        const data = await response.json();
        
        updateBreadcrumb();
        fileList.innerHTML = '';
        
        if (data.files && data.files.length > 0) {
            data.files.forEach(file => {
                const item = document.createElement('div');
                item.className = `file-item ${file.isDir ? 'folder' : 'file'}`;
                
                const icon = file.isDir ? '📁' : '📄';
                const sizeText = file.isDir ? 'DIR' : formatBytes(file.size);
                
                item.innerHTML = `
                    <span class="file-icon">${icon}</span>
                    <span class="file-name">${file.name}</span>
                    <span class="file-size">${sizeText}</span>
                    <div class="file-actions">
                        ${!file.isDir ? `<button class="btn-icon compare-btn" onclick="initiateCompare('${file.name}')" title="Compare">🔎</button>` : ''}    
                        ${!file.isDir ? `<button class="btn-icon" onclick="editFile('${file.name}')" title="Edit">✏️</button>` : ''}
                        ${!file.isDir ? `<button class="btn-icon" onclick="downloadFile('${file.name}')" title="Download">⬇️</button>` : ''}
                        <button class="btn-icon" onclick="deleteFile('${file.name}')" title="Delete">🗑️</button>
                    </div>
                `;
                
                // Click handler for folders
                if (file.isDir) {
                    item.addEventListener('click', (e) => {
                        if (!e.target.closest('.file-actions')) {
                            navigateToFolder(file.name);
                        }
                    });
                }
                
                fileList.appendChild(item);
            });
        } else {
            fileList.innerHTML = '<div style="padding: 2rem; text-align: center; color: var(--text-muted)">No files found</div>';
        }
        
        // Update storage info
        if (data.total && data.used !== undefined) {
            const usedPercent = (data.used / data.total) * 100;
            document.getElementById('storage-bar-fill').style.width = `${usedPercent}%`;
            document.getElementById('storage-text').textContent = `${formatBytes(data.used)} / ${formatBytes(data.total)}`;
        }
    } catch (error) {
        console.error('Error loading files:', error);
        fileList.innerHTML = '<div style="padding: 2rem; text-align: center; color: var(--danger)">Error loading files</div>';
    }
}


// Navigate into a folder
function navigateToFolder(folderName) {
    if (currentPath === '/') {
        currentPath = '/' + folderName;
    } else {
        currentPath = currentPath + '/' + folderName;
    }
    refreshFiles();
}

// Navigate to specific path
function navigateToPath(path) {
    currentPath = path;
    refreshFiles();
}

// Update breadcrumb navigation
function updateBreadcrumb() {
    breadcrumb.innerHTML = '';
    
    // Home item
    const homeItem = document.createElement('span');
    homeItem.className = 'breadcrumb-item' + (currentPath === '/' ? ' active' : '');
    homeItem.innerHTML = '🏠 Home';
    homeItem.dataset.path = '/';
    
    if (currentPath !== '/') {
        homeItem.addEventListener('click', () => navigateToPath('/'));
    }
    
    breadcrumb.appendChild(homeItem);
    
    // Path segments
    if (currentPath !== '/') {
        const segments = currentPath.split('/').filter(s => s.length > 0);
        let builtPath = '';
        
        segments.forEach((segment, index) => {
            builtPath += '/' + segment;
            const isLast = index === segments.length - 1;
            
            // Separator
            const sep = document.createElement('span');
            sep.className = 'breadcrumb-separator';
            sep.textContent = '›';
            breadcrumb.appendChild(sep);
            
            // Segment item
            const segmentItem = document.createElement('span');
            segmentItem.className = 'breadcrumb-item' + (isLast ? ' active' : '');
            segmentItem.textContent = segment;
            segmentItem.dataset.path = builtPath;
            
            if (!isLast) {
                const pathToNavigate = builtPath;
                segmentItem.addEventListener('click', () => navigateToPath(pathToNavigate));
            }
            
            breadcrumb.appendChild(segmentItem);
        });
    }
}

// Delete file
window.deleteFile = function(filename) {
    const fullPath = currentPath === '/' ? '/' + filename : currentPath + '/' + filename;
    
    openModal(
        'Delete File',
        `<p>Are you sure you want to delete <strong>${filename}</strong>?</p><p style="color: var(--danger)">This action cannot be undone.</p>`,
        async () => {
            try {
                const response = await fetch(`/api/files/delete?path=${encodeURIComponent(fullPath)}`, {
                    method: 'DELETE'
                });
                
                if (response.ok) {
                    console.log('File deleted');
                    refreshFiles();
                } else {
                    alert('Failed to delete file');
                }
            } catch (error) {
                console.error('Error:', error);
            }
        }
    );
};

// Download file
window.downloadFile = function(filename) {
    const fullPath = currentPath === '/' ? '/' + filename : currentPath + '/' + filename;
    window.location.href = `/download?path=${encodeURIComponent(fullPath)}`;
};

btnRefresh.addEventListener('click', refreshFiles);

// Upload file
btnUpload.addEventListener('click', () => fileUploadInput.click());
fileUploadInput.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    
    const formData = new FormData();
    formData.append('file', file);
    
    // Send path as query parameter in URL
    const uploadUrl = `/upload?path=${encodeURIComponent(currentPath)}`;
    
    try {
        const response = await fetch(uploadUrl, { method: 'POST', body: formData });
        if (response.ok) {
            console.log('File uploaded successfully');
            refreshFiles();
        } else {
            alert('Upload failed');
        }
    } catch (error) {
        console.error('Upload error:', error);
        alert('Upload error');
    }
    
    fileUploadInput.value = '';
});

// Create new file
btnNewFile.addEventListener('click', () => {
    openModal(
        'Create New File',
        `<div class="input-group">
            <label>File Name</label>
            <input type="text" id="new-file-name" class="text-input" placeholder="example.txt">
        </div>
        <div class="input-group">
            <label>Content (optional)</label>
            <textarea id="new-file-content" class="text-input" rows="5" placeholder="File content..."></textarea>
        </div>`,
        async () => {
            const name = document.getElementById('new-file-name').value.trim();
            const content = document.getElementById('new-file-content').value;
            
            if (!name) {
                alert('Please enter a file name');
                return;
            }
            
            // Create file in current path
            const fullPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
            
            try {
                const formData = new FormData();
                formData.append('name', fullPath);
                formData.append('content', content);
                
                const response = await fetch('/api/files', { method: 'POST', body: formData });
                
                if (response.ok) {
                    console.log('File created');
                    refreshFiles();
                } else {
                    alert('Failed to create file');
                }
            } catch (error) {
                console.error('Error:', error);
            }
        }
    );
});

// Create new folder
btnNewFolder.addEventListener('click', () => {
    openModal(
        'Create New Folder',
        `<div class="input-group">
            <label>Folder Name</label>
            <input type="text" id="new-folder-name" class="text-input" placeholder="myfolder">
        </div>`,
        async () => {
            const name = document.getElementById('new-folder-name').value.trim();
            
            if (!name) {
                alert('Please enter a folder name');
                return;
            }
            
            // Create folder in current path
            const fullPath = currentPath === '/' ? '/' + name : currentPath + '/' + name;
            
            try {
                const formData = new FormData();
                formData.append('name', fullPath);
                
                const response = await fetch('/api/files/mkdir', { method: 'POST', body: formData });
                
                if (response.ok) {
                    console.log('Folder created');
                    refreshFiles();
                } else {
                    alert('Failed to create folder');
                }
            } catch (error) {
                console.error('Error:', error);
            }
        }
    );
});

// ============================================
// LOGOUT
// ============================================

const btnLogout = document.getElementById('btn-logout');

btnLogout.addEventListener('click', async () => {
    try {
        const response = await fetch('/logout', { method: 'POST' });
        if (response.ok) {
            // Redirect at login
            window.location.href = '/login';
        } else {
            alert('Logout failed');
        }
    } catch (error) {
        console.error('Logout error:', error);
        // Try redirecting anyway
        window.location.href = '/login';
    }
});


// ============================================
// Settings Functions
// ============================================
const btnSaveWifi = document.getElementById('btn-save-wifi');
const btnClearWifi = document.getElementById('btn-clear-wifi');
const btnSaveSettings = document.getElementById('btn-save-settings');
const btnReboot = document.getElementById('btn-reboot');
const btnFormatFS = document.getElementById('btn-format-fs');

// Save WiFi credentials
btnSaveWifi.addEventListener('click', async () => {
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const pass = document.getElementById('wifi-pass').value;
  const mode = document.querySelector('input[name="wifi-mode"]:checked').value;
  
  if (!ssid) {
    alert('Please enter SSID');
    return;
  }
  
  try {
    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('pass', pass);
    formData.append('mode', mode);
    
    const response = await fetch('/api/wifi/add', {
      method: 'POST',
      body: formData
    });
    
    if (response.ok) {
      alert('WiFi configuration saved');
      document.getElementById('wifi-ssid').value = '';
      document.getElementById('wifi-pass').value = '';
    } else {
      alert('Failed to save WiFi');
    }
  } catch (error) {
    console.error('Error:', error);
  }
});

// Clear WiFi DB
btnClearWifi.addEventListener('click', () => {
  openModal(
    'Clear WiFi Database',
    '<p>Are you sure you want to clear all saved WiFi credentials?</p>',
    async () => {
      try {
        const response = await fetch('/api/wifi/clear', {
          method: 'POST'
        });
        
        if (response.ok) {
          alert('WiFi database cleared');
        }
      } catch (error) {
        console.error('Error:', error);
      }
    }
  );
});

// Save settings
btnSaveSettings.addEventListener('click', async () => {
  const deviceName = document.getElementById('device-name').value.trim();
  
  try {
    const formData = new FormData();
    formData.append('deviceName', deviceName);
    
    const response = await fetch('/api/settings', {
      method: 'POST',
      body: formData
    });
    
    if (response.ok) {
      alert('Settings saved');
    } else {
      alert('Failed to save settings');
    }
  } catch (error) {
    console.error('Error:', error);
  }
});

// Backup LittleFS
const btnBackup = document.getElementById('btn-backup');
btnBackup.addEventListener('click', async () => {
    const confirmBackup = confirm('Download a ZIP backup of all files in LittleFS?');
    if (!confirmBackup) return;
    
    try {
        // Show loading
        btnBackup.disabled = true;
        btnBackup.textContent = '⏳ Creating backup...';
        
        // Download the ZIP backup
        const timestamp = new Date().toISOString().slice(0, 19).replace(/:/g, '-');
        const filename = `ESP32_Backup_${timestamp}.zip`;
        
        window.location.href = `/api/backup?filename=${encodeURIComponent(filename)}`;
        
        // Reset button after 5 seconds
        setTimeout(() => {
            btnBackup.disabled = false;
            btnBackup.textContent = 'BACKUP DATA';
        }, 5000);
    } catch (error) {
        console.error('Backup error:', error);
        alert('Backup failed');
        btnBackup.disabled = false;
        btnBackup.textContent = 'BACKUP DATA';
    }
});

// Reboot device
btnReboot.addEventListener('click', () => {
  openModal(
    'Reboot Device',
    '<p>Are you sure you want to reboot the device?</p><p style="color: var(--warning);">The connection will be lost temporarily.</p>',
    async () => {
      try {
        await fetch('/api/reboot', { method: 'POST' });
        alert('Device rebooting...');
        setTimeout(() => location.reload(), 5000);
      } catch (error) {
        console.error('Error:', error);
      }
    }
  );
});

// Format LittleFS
btnFormatFS.addEventListener('click', () => {
  openModal(
    'Format LittleFS',
    '<p style="color: var(--danger); font-weight: bold;">⚠️ WARNING ⚠️</p><p>This will delete ALL files on the filesystem!</p><p>This action cannot be undone.</p>',
    async () => {
      try {
        const response = await fetch('/api/format', { method: 'POST' });
        if (response.ok) {
          alert('Filesystem formatted');
          refreshFiles();
        }
      } catch (error) {
        console.error('Error:', error);
      }
    }
  );
});

// ============================================
// Status & Utility Functions
// ============================================

// Format bytes
function formatBytes(bytes) {
  if (bytes === 0) return '0 B';
  const k = 1024;
  const sizes = ['B', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// Update system status
async function updateStatus() {
  try {
    const response = await fetch('/api/status');
    const data = await response.json();
    
    if (data.ip) {
      document.getElementById('footer-ip').textContent = `IP: ${data.ip}`;
    }
    if (data.freeHeap) {
      document.getElementById('free-heap').textContent = formatBytes(data.freeHeap);
    }
    if (data.chipModel) {
      document.getElementById('chip-model').textContent = data.chipModel;
    }
    if (data.wifiStatus) {
      const wifiMode = document.getElementById('wifi-mode');
      wifiMode.textContent = data.wifiStatus;
    }
    if (data.debugMode) {
            const statusBar = document.querySelector('.status-bar');
            if (!document.getElementById('debug-warning')) {
                const warning = document.createElement('div');
                warning.id = 'debug-warning';
                warning.style.color = 'var(--warning)';
                warning.style.fontWeight = 'bold';
                warning.textContent = '⚠️ DEBUG MODE';
                statusBar.prepend(warning);
            }
        }
  } catch (error) {
    console.error('Status update error:', error);
  }
}

// Uptime counter
let uptimeSeconds = 0;
function updateUptime() {
  uptimeSeconds++;
  const hours = Math.floor(uptimeSeconds / 3600).toString().padStart(2, '0');
  const minutes = Math.floor((uptimeSeconds % 3600) / 60).toString().padStart(2, '0');
  const seconds = (uptimeSeconds % 60).toString().padStart(2, '0');
  document.getElementById('uptime').textContent = `Uptime: ${hours}:${minutes}:${seconds}`;
}

setInterval(updateUptime, 1000);
setInterval(updateStatus, 10000);

// ============================================
// Keyboard Shortcuts
// ============================================
document.addEventListener('keydown', (e) => {
  // ESC to close modal
  if (e.key === 'Escape' && modal.classList.contains('active')) {
    closeModal();
  }
  
  // Ctrl+1-2 to switch tabs
  if (e.ctrlKey && e.key >= '1' && e.key <= '2') {
    e.preventDefault();
    const tabIndex = parseInt(e.key) - 1;
    if (tabs[tabIndex]) {
      tabs[tabIndex].click();
    }
  }
  
  // Ctrl+R to refresh files
  if (e.ctrlKey && e.key === 'r' && document.getElementById('file-manager').classList.contains('active')) {
    e.preventDefault();
    refreshFiles();
  }
});

// ============================================
// Initial Load
// ============================================
refreshFiles();
updateStatus();

// ============================================
// FILE EDITOR FUNCTIONS
// ============================================

const editorModal = document.getElementById('editor-modal');
const editorClose = document.getElementById('editor-close');
const editorTextarea = document.getElementById('file-content');
const lineNumbers = document.getElementById('line-numbers');
const editorPath = document.getElementById('editor-path');
const editorSize = document.getElementById('editor-size');
const editorCharCount = document.getElementById('editor-char-count');
const editorModified = document.getElementById('editor-modified');
const editorAutosaveStatus = document.getElementById('editor-autosave-status');

const btnSave = document.getElementById('btn-save');
const btnDiscard = document.getElementById('btn-discard');
const btnFindToggle = document.getElementById('btn-find-toggle');
const btnDiffView = document.getElementById('btn-diff-view');


const findReplacePanel = document.getElementById('find-replace-panel');
const findInput = document.getElementById('find-input');
const replaceInput = document.getElementById('replace-input');
const btnFindPrev = document.getElementById('btn-find-prev');
const btnFindNext = document.getElementById('btn-find-next');
const btnReplace = document.getElementById('btn-replace');
const btnReplaceAll = document.getElementById('btn-replace-all');
const closeFindBtn = document.getElementById('close-find');
const findCount = document.getElementById('find-count');

const diffModal = document.getElementById('diff-modal');
const diffClose = document.getElementById('diff-close');
const diffContent = document.getElementById('diff-content');
const btnBackToEdit = document.getElementById('btn-back-to-edit');
const btnSaveFromDiff = document.getElementById('btn-save-from-diff');

let currentFilePath = '';
let originalContent = '';
let isDirty = false;
let autosaveInterval = null;
let findMatches = [];
let currentMatchIndex = -1;
let currentEditorPath = "";

// Open File Editor
window.editFile = async function(filename) {
    const fullPath = currentPath === '/' ? '/' + filename : currentPath + '/' + filename;
    currentEditorPath = currentPath === '/' ? '/' + filename : currentPath + '/' + filename;
    try {
        console.log('📖 Reading file:', fullPath);
        
        // Show loading
        editorPath.textContent = 'Loading...';
        editorTextarea.value = 'Loading file...';
        editorModal.classList.add('active');
        
        // Increase timeout for large files
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 5000); // 5 seconds
        
        const response = await fetch(`/api/files/read?path=${encodeURIComponent(fullPath)}`, {
            signal: controller.signal
        });
        clearTimeout(timeoutId);
        
        console.log('📡 Response status:', response.status, response.statusText);
        
        if (!response.ok) {
            const errorText = await response.text();
            console.error('❌ Error response:', errorText);
            editorModal.classList.remove('active');
            alert('Failed to read file: ' + response.statusText);
            return;
        }
        
        const data = await response.json();
        console.log('📦 Received data size:', data.size, 'bytes');
        
        if (!data || data.content === undefined) {
            console.error('❌ Invalid data structure:', data);
            editorModal.classList.remove('active');
            alert('Invalid response from server');
            return;
        }
        
        currentFilePath = fullPath;
        originalContent = data.content;
        
        // Set UI
        editorPath.textContent = fullPath;
        editorSize.textContent = formatBytes(data.size);
        editorTextarea.value = data.content;
        
        // Reset state
        isDirty = false;
        updateEditorStatus();
        updateLineNumbers();
        updateCharCount();
        
        // Check for draft
        loadDraft();
        
        editorTextarea.focus();
        startAutosave();
        
    } catch (error) {
        console.error('💥 Error loading file:', error);
        editorModal.classList.remove('active');
        
        if (error.name === 'AbortError') {
            alert('Request timeout - file too large or server not responding');
        } else {
            alert('Error loading file: ' + error.message);
        }
    }
};


// Close Editor
function closeEditor() {
    if (isDirty) {
        const confirmClose = confirm('You have unsaved changes. Discard them?');
        if (!confirmClose) return;
    }
    
    stopAutosave();
    clearDraft();
    editorModal.classList.remove('active');
    findReplacePanel.classList.add('hidden');
}

editorClose.addEventListener('click', closeEditor);
btnDiscard.addEventListener('click', closeEditor);

// Update Line Numbers
function updateLineNumbers() {
    const lines = editorTextarea.value.split('\n').length;
    let lineHtml = '';
    for (let i = 1; i <= lines; i++) {
        lineHtml += i + '\n';
    }
    lineNumbers.textContent = lineHtml;
}

// Sync Scroll
editorTextarea.addEventListener('scroll', () => {
    lineNumbers.scrollTop = editorTextarea.scrollTop;
});

// Update on Input
editorTextarea.addEventListener('input', () => {
    isDirty = true;
    updateLineNumbers();
    updateCharCount();
    updateEditorStatus();
});

// Update Character Count
function updateCharCount() {
    const chars = editorTextarea.value.length;
    editorCharCount.textContent = `${chars} chars`;
}

// Update Editor Status
function updateEditorStatus() {
    if (isDirty) {
        editorModified.textContent = 'Modified';
        editorModified.classList.add('modified');
    } else {
        editorModified.textContent = 'Not modified';
        editorModified.classList.remove('modified');
    }
}

// Save File
async function saveFile() {
    if (!isDirty) {
        alert('No changes to save');
        return;
    }
    
    const confirmSave = confirm('Are you sure you want to save changes?');
    if (!confirmSave) return;
    
    try {
        const formData = new FormData();
        formData.append('path', currentFilePath);
        formData.append('content', editorTextarea.value);
        
        const response = await fetch('/api/files/update', {
            method: 'PUT',
            body: formData
        });
        
        if (response.ok) {
            originalContent = editorTextarea.value;
            isDirty = false;
            updateEditorStatus();
            clearDraft();
            alert('File saved successfully!');
            refreshFiles();
        } else {
            alert('Failed to save file');
        }
    } catch (error) {
        console.error('Save error:', error);
        alert('Error saving file');
    }
}

btnSave.addEventListener('click', saveFile);

// Autosave to localStorage
function startAutosave() {
    stopAutosave();
    autosaveInterval = setInterval(() => {
        if (isDirty) {
            saveDraft();
            editorAutosaveStatus.textContent = '✓';
            setTimeout(() => {
                editorAutosaveStatus.textContent = '●';
            }, 2000);
        }
    }, 10000); // 10 seconds
}

function stopAutosave() {
    if (autosaveInterval) {
        clearInterval(autosaveInterval);
        autosaveInterval = null;
    }
}

function saveDraft() {
    const draftKey = 'draft_' + currentFilePath;
    localStorage.setItem(draftKey, editorTextarea.value);
}

function loadDraft() {
    const draftKey = 'draft_' + currentFilePath;
    const draft = localStorage.getItem(draftKey);
    
    if (draft && draft !== originalContent) {
        const useDraft = confirm('A draft was found. Load it?');
        if (useDraft) {
            editorTextarea.value = draft;
            isDirty = true;
            updateEditorStatus();
            updateLineNumbers();
            updateCharCount();
        }
    }
}

function clearDraft() {
    const draftKey = 'draft_' + currentFilePath;
    localStorage.removeItem(draftKey);
}

// Find & Replace
btnFindToggle.addEventListener('click', () => {
    findReplacePanel.classList.toggle('hidden');
    if (!findReplacePanel.classList.contains('hidden')) {
        findInput.focus();
    }
});

closeFindBtn.addEventListener('click', () => {
    findReplacePanel.classList.add('hidden');
    clearHighlights();
});

// Find functionality
findInput.addEventListener('input', () => {
    performFind();
});

function performFind() {
    const searchTerm = findInput.value;
    clearHighlights();
    
    if (!searchTerm) {
        findCount.textContent = '0/0';
        return;
    }
    
    const content = editorTextarea.value;
    findMatches = [];
    
    let index = content.indexOf(searchTerm);
    while (index !== -1) {
        findMatches.push(index);
        index = content.indexOf(searchTerm, index + 1);
    }
    
    if (findMatches.length > 0) {
        currentMatchIndex = 0;
        highlightMatch();
    }
    
    findCount.textContent = findMatches.length > 0 
        ? `${currentMatchIndex + 1}/${findMatches.length}` 
        : '0/0';
}

btnFindNext.addEventListener('click', (e) => {
    e.preventDefault();
    if (findMatches.length === 0) return;
    currentMatchIndex = (currentMatchIndex + 1) % findMatches.length;
    highlightMatch();
});

btnFindPrev.addEventListener('click', (e) => {
    e.preventDefault();
    if (findMatches.length === 0) return;
    currentMatchIndex = currentMatchIndex - 1;
    if (currentMatchIndex < 0) currentMatchIndex = findMatches.length - 1;
    highlightMatch();
});

function highlightMatch() {
    if (findMatches.length === 0) return;
    
    const pos = findMatches[currentMatchIndex];
    const searchTerm = findInput.value;
    
    // FOCUS on the text area to make the selection visible
    editorTextarea.focus();
    
    // Select the text
    editorTextarea.setSelectionRange(pos, pos + searchTerm.length);
    
    // Scroll to position
    const textBeforeMatch = editorTextarea.value.substring(0, pos);
    const lineNumber = textBeforeMatch.split('\n').length;
    const lineHeight = parseFloat(getComputedStyle(editorTextarea).lineHeight) || 21;
    const findPanelHeight = findReplacePanel.classList.contains('hidden') ? 0 : 80;
    const offsetLines = Math.ceil(findPanelHeight / lineHeight) + 4;
    const targetScrollLine = Math.max(0, lineNumber - offsetLines);
    editorTextarea.scrollTop = targetScrollLine * lineHeight;
    
    findCount.textContent = `${currentMatchIndex + 1}/${findMatches.length}`;
}

// Intercept typing when Find is open and textarea has focus
editorTextarea.addEventListener('keydown', (e) => {
    if (!findReplacePanel.classList.contains('hidden')) {
        const isNormalKey = e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey;
        const isBackspace = e.key === 'Backspace';
        
        if (isNormalKey || isBackspace) {
            e.preventDefault();
            
            // Update findInput
            if (isBackspace) {
                findInput.value = findInput.value.slice(0, -1);
            } else {
                findInput.value += e.key;
            }
            
            // Trigger search
            findInput.dispatchEvent(new Event('input'));
            
            // Restore focus to the text area after refreshing the search
            setTimeout(() => {
                editorTextarea.focus();
                if (findMatches.length > 0) {
                    const pos = findMatches[currentMatchIndex];
                    const searchTerm = findInput.value;
                    editorTextarea.setSelectionRange(pos, pos + searchTerm.length);
                }
            }, 0);
        } else if (e.key === 'Enter') {
            e.preventDefault();
            if (e.shiftKey) {
                btnFindPrev.click();
            } else {
                btnFindNext.click();
            }
        } else if (e.key === 'Escape') {
            e.preventDefault();
            closeFindBtn.click();
        }
    }
});



function clearHighlights() {
    findMatches = [];
    currentMatchIndex = -1;
}

// Replace functionality
btnReplace.addEventListener('click', () => {
    if (findMatches.length === 0 || currentMatchIndex === -1) return;
    
    const searchTerm = findInput.value;
    const replaceTerm = replaceInput.value;
    const content = editorTextarea.value;
    const pos = findMatches[currentMatchIndex];
    
    const newContent = content.substring(0, pos) + replaceTerm + content.substring(pos + searchTerm.length);
    editorTextarea.value = newContent;
    isDirty = true;
    updateEditorStatus();
    updateLineNumbers();
    updateCharCount();
    
    // Save the current index
    const oldIndex = currentMatchIndex;
    
    // Re-find (this updates findMatches)
    performFind();
    
    // Go to the next match (or stay at the last one if it was the last occurrence)
    if (findMatches.length > 0) {
        //Stay on the same index (which now points to the next match after the substitution)
        if (oldIndex >= findMatches.length) {
            currentMatchIndex = findMatches.length - 1;
        } else {
            currentMatchIndex = oldIndex;
        }
        highlightMatch();
    }
});

btnReplaceAll.addEventListener('click', () => {
    if (findMatches.length === 0) return;
    
    const confirmReplace = confirm(`Replace all ${findMatches.length} occurrences?`);
    if (!confirmReplace) return;
    
    const searchTerm = findInput.value;
    const replaceTerm = replaceInput.value;
    
    editorTextarea.value = editorTextarea.value.split(searchTerm).join(replaceTerm);
    
    isDirty = true;
    updateEditorStatus();
    updateLineNumbers();
    updateCharCount();
    
    performFind();
});

// Diff View
btnDiffView.addEventListener('click', () => {
    showDiff();
});

function showDiff() {
    //Normalize both contents for accurate comparison
    const normalizeContent = (str) => {
        return str
            .replace(/\r\n/g, '\n')  // Windows -> Unix line endings
            .replace(/\r/g, '\n')    // Mac -> Unix line endings
            .trim();                  // Remove leading and trailing spaces/newlines
    };
    
    const originalNormalized = normalizeContent(originalContent);
    const modifiedNormalized = normalizeContent(editorTextarea.value);
    
    // DEBUG: Print to check
    console.log('🔍 DIFF COMPARISON:');
    console.log('Original length:', originalNormalized.length);
    console.log('Modified length:', modifiedNormalized.length);
    console.log('Are equal?', originalNormalized === modifiedNormalized);
    
    // Direct comparison of normalized strings
    if (originalNormalized === modifiedNormalized) {
        diffContent.innerHTML = `
            <div style="text-align: center; padding: 3rem; color: var(--text-muted);">
                <div style="font-size: 3rem; margin-bottom: 1rem;">✓</div>
                <div style="font-size: 1.2rem; color: var(--accent);">No changes detected</div>
                <div style="font-size: 0.9rem; margin-top: 0.5rem;">The file content is identical to the original.</div>
            </div>
        `;
        diffModal.classList.add('active');
        return;
    }
    
    // If there are changes, generate the diff
    const original = originalNormalized.split('\n');
    const modified = modifiedNormalized.split('\n');
    
    let diffHtml = '';
    const changedLines = new Set();
    const maxLines = Math.max(original.length, modified.length);
    
    // Find changed lines
    for (let i = 0; i < maxLines; i++) {
        const origLine = original[i] !== undefined ? original[i] : '';
        const modLine = modified[i] !== undefined ? modified[i] : '';
        
        if (origLine !== modLine) {
            // Add context (3 lines before and after)
            for (let j = Math.max(0, i - 3); j <= Math.min(maxLines - 1, i + 3); j++) {
                changedLines.add(j);
            }
        }
    }
    
    // Generate diff
    let lastShownLine = -1;
    const sortedLines = Array.from(changedLines).sort((a, b) => a - b);
    
    sortedLines.forEach(i => {
        // Skipped line separator
        if (lastShownLine !== -1 && i > lastShownLine + 1) {
            const gapSize = i - lastShownLine - 1;
            diffHtml += `<div class="diff-separator">
                ⋯ ${gapSize} unchanged line${gapSize > 1 ? 's' : ''} ⋯
            </div>`;
        }
        
        const origLine = original[i] !== undefined ? original[i] : '';
        const modLine = modified[i] !== undefined ? modified[i] : '';
        
        if (origLine === modLine) {
            // Unchanged line (context)
            diffHtml += `<div class="diff-line unchanged">
                <span class="diff-line-number">${i + 1}</span>${escapeHtml(origLine) || '(empty line)'}
            </div>`;
        } else {
            // Modified row
            if (original[i] !== undefined) {
                diffHtml += `<div class="diff-line removed">
                    <span class="diff-line-number">${i + 1}</span>- ${escapeHtml(origLine) || '(empty line)'}
                </div>`;
            }
            if (modified[i] !== undefined) {
                diffHtml += `<div class="diff-line added">
                    <span class="diff-line-number">${i + 1}</span>+ ${escapeHtml(modLine) || '(empty line)'}
                </div>`;
            }
        }
        
        lastShownLine = i;
    });
    
    diffContent.innerHTML = diffHtml;
    diffModal.classList.add('active');
}


function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

diffClose.addEventListener('click', () => {
    diffModal.classList.remove('active');
});

btnBackToEdit.addEventListener('click', () => {
    diffModal.classList.remove('active');
});

btnSaveFromDiff.addEventListener('click', () => {
    diffModal.classList.remove('active');
    saveFile();
});

// Keyboard Shortcuts
editorModal.addEventListener('keydown', (e) => {
    // Ctrl+S to save
    if (e.ctrlKey && e.key === 's') {
        e.preventDefault();
        saveFile();
    }
    
    // Ctrl+F to find
    if (e.ctrlKey && e.key === 'f') {
        e.preventDefault();
        findReplacePanel.classList.remove('hidden');
        findInput.focus();
    }
    
    // Ctrl+H to replace
    if (e.ctrlKey && e.key === 'h') {
        e.preventDefault();
        findReplacePanel.classList.remove('hidden');
        replaceInput.focus();
    }
    
    // ESC to close find panel
    if (e.key === 'Escape' && !findReplacePanel.classList.contains('hidden')) {
        findReplacePanel.classList.add('hidden');
        clearHighlights();
        e.stopPropagation();
    }
});

// Prevent accidental close
window.addEventListener('beforeunload', (e) => {
    if (isDirty && editorModal.classList.contains('active')) {
        e.preventDefault();
        e.returnValue = '';
    }
});

// Force selection to stay visible
let currentSelection = null;

function maintainSelection() {
    if (currentSelection && !findReplacePanel.classList.contains('hidden')) {
        editorTextarea.setSelectionRange(currentSelection.start, currentSelection.end);
    }
}

// Save selection when it changes
document.addEventListener('selectionchange', () => {
    if (document.activeElement === editorTextarea || !findReplacePanel.classList.contains('hidden')) {
        const start = editorTextarea.selectionStart;
        const end = editorTextarea.selectionEnd;
        if (start !== end) {
            currentSelection = { start, end };
        }
    }
});

// Keep selection when focus changes
editorTextarea.addEventListener('blur', () => {
    if (!findReplacePanel.classList.contains('hidden')) {
        setTimeout(maintainSelection, 0);
    }
});

// ============================================
// RENAME FUNCTIONALITY
// ============================================

const renamePrompt = document.getElementById('rename-prompt');
const renameInput = document.getElementById('rename-input');
const renameWarning = document.getElementById('rename-warning');
const btnRenameFile = document.getElementById('btn-rename-file');
const btnSaveRename = document.getElementById('btn-save-rename');
const btnDiscardRename = document.getElementById('btn-discard-rename');

let originalRenameExtension = '';

function openRenamePrompt() {

    const filename = currentEditorPath.split('/').pop();
    originalRenameExtension = filename.includes('.') ? filename.split('.').pop() : '';

    renameInput.value = filename;
    renameWarning.classList.remove('visible');
    renamePrompt.style.display = 'flex';
    renameInput.focus();
    
    // Select only the name, not the extension
    const dotIndex = filename.lastIndexOf('.');
    if(dotIndex > 0) {
        renameInput.setSelectionRange(0, dotIndex);
    }
}

function closeRenamePrompt() {
    renamePrompt.style.display = 'none';
}


btnRenameFile.addEventListener('click', openRenamePrompt);

// Listener to monitor extension changes
renameInput.addEventListener('input', () => {
    const newName = renameInput.value;
    const newExt = newName.includes('.') ? newName.split('.').pop() : '';
    
    if (newExt !== originalRenameExtension) {
        renameWarning.classList.add('visible');
        renameWarning.textContent = `⚠️ Extension changing: .${originalRenameExtension} -> .${newExt}`;
    } else {
        renameWarning.classList.remove('visible');
    }
});

btnDiscardRename.addEventListener('click', closeRenamePrompt);

btnSaveRename.addEventListener('click', async () => {
    const newFilename = renameInput.value.trim();
    if (!newFilename) return;

    // Retrieve the current folder from the path of the file being edited
    const lastSlashIndex = currentEditorPath.lastIndexOf('/');
    const directory = currentEditorPath.substring(0, lastSlashIndex);
    const newFullPath = (directory === '' ? '' : directory) + '/' + newFilename;

    try {
        const response = await fetch(`/api/files/rename?oldPath=${encodeURIComponent(currentEditorPath)}&newPath=${encodeURIComponent(newFullPath)}`, {
            method: 'POST'
        });

        if (response.ok) {
            closeRenamePrompt();
            closeEditor(); 
            refreshFiles(); 
        } else {
            const err = await response.json();
            alert("Error: " + (err.error || "Rename failed"));
        }
    } catch (e) {
        alert("Connection error");
    }
});

// ============================================
// COMPARE MODE FUNCTIONS
// ============================================

let compareSourcePath = null;
const compareNotification = document.getElementById('compare-notification');
const compareSourceNameDisplay = document.getElementById('compare-source-name');
const btnCancelCompare = document.getElementById('btn-cancel-compare');
const compareModal = document.getElementById('compare-modal');
const compareClose = document.getElementById('compare-close');
const btnCloseCompare = document.getElementById('btn-close-compare');

// Panes
const cmpScrollLeft = document.getElementById('cmp-scroll-left');
const cmpScrollRight = document.getElementById('cmp-scroll-right');
const cmpHeaderLeft = document.getElementById('cmp-header-left');
const cmpHeaderRight = document.getElementById('cmp-header-right');

// 1. Click on Magnifying Glass
window.initiateCompare = function(filename) {
    const fullPath = currentPath === '/' ? '/' + filename : currentPath + '/' + filename;

    // If we are already selecting and click another file, that is the TARGET
    if (compareSourcePath) {
        if (compareSourcePath === fullPath) {
            // Clicked the same file: cancel
            cancelCompareSelection();
        } else {
            // Run comparison
            runComparison(compareSourcePath, fullPath);
        }
    } else {
        // First click: Select SOURCE
        compareSourcePath = fullPath;
        showCompareNotification(filename);
        highlightSourceFile(filename);
    }
};

function showCompareNotification(filename) {
    compareSourceNameDisplay.textContent = filename;
    compareNotification.classList.add('active');
}

function cancelCompareSelection() {
    compareSourcePath = null;
    compareNotification.classList.remove('active');
    // Remove visual style from file list
    document.querySelectorAll('.file-item').forEach(el => el.classList.remove('comparing-source'));
}

// Visual highlight of the source file in the list
function highlightSourceFile(filename) {
    // Remove previous
    document.querySelectorAll('.file-item').forEach(el => el.classList.remove('comparing-source'));
    
    // Find the item in the list
    const items = document.querySelectorAll('.file-item');
    items.forEach(item => {
        const nameSpan = item.querySelector('.file-name');
        if (nameSpan && nameSpan.textContent === filename) {
            item.classList.add('comparing-source');
        }
    });
}

btnCancelCompare.addEventListener('click', cancelCompareSelection);

// 2. Fetch and Compare
async function runComparison(pathA, pathB) {
    try {
        // Reset UI selection
        cancelCompareSelection();
        
        // Loading...
        openModal('Comparing...', '<div class="loading">Reading files...</div>');
        
        // Parallel fetch of the two files
        const [resA, resB] = await Promise.all([
            fetch(`/api/files/read?path=${encodeURIComponent(pathA)}`),
            fetch(`/api/files/read?path=${encodeURIComponent(pathB)}`)
        ]);

        if (!resA.ok || !resB.ok) {
            throw new Error('Failed to read one of the files');
        }

        const dataA = await resA.json();
        const dataB = await resB.json();

        // Close modale loading
        closeModal();

        // Open modale Compare
        openCompareModal(pathA, dataA.content, pathB, dataB.content);

    } catch (error) {
        closeModal(); // Close loading
        console.error("Compare error:", error);
        alert("Error comparing files: " + error.message);
    }
}

// 3. Render Side-by-Side
function openCompareModal(nameA, contentA, nameB, contentB) {
    cmpHeaderLeft.textContent = nameA;
    cmpHeaderRight.textContent = nameB;
    
    // Normalize newline
    const linesA = contentA.replace(/\r\n/g, '\n').split('\n');
    const linesB = contentB.replace(/\r\n/g, '\n').split('\n');
    
    const maxLines = Math.max(linesA.length, linesB.length);
    
    let htmlLeft = '';
    let htmlRight = '';
    
    for (let i = 0; i < maxLines; i++) {
        const lineA = linesA[i] !== undefined ? linesA[i] : null;
        const lineB = linesB[i] !== undefined ? linesB[i] : null;
        
        // Comparison logic: If different or one is missing, flag as DIFF
        const isDiff = (lineA !== lineB);
        const diffClass = isDiff ? ' diff' : '';
        
        // Left Column
        if (lineA !== null) {
            htmlLeft += `
                <div class="cmp-row${diffClass}">
                    <div class="cmp-number">${i + 1}</div>
                    <div class="cmp-content">${escapeHtmlCompare(lineA)}</div>
                </div>`;
        } else {
            // Blank line (alignment filler)
            htmlLeft += `
                <div class="cmp-row cmp-empty">
                    <div class="cmp-number"></div>
                    <div class="cmp-content"></div>
                </div>`;
        }
        
        // Right Column
        if (lineB !== null) {
            htmlRight += `
                <div class="cmp-row${diffClass}">
                    <div class="cmp-number">${i + 1}</div>
                    <div class="cmp-content">${escapeHtmlCompare(lineB)}</div>
                </div>`;
        } else {
            htmlRight += `
                <div class="cmp-row cmp-empty">
                    <div class="cmp-number"></div>
                    <div class="cmp-content"></div>
                </div>`;
        }
    }
    
    cmpScrollLeft.innerHTML = htmlLeft;
    cmpScrollRight.innerHTML = htmlRight;
    
    compareModal.classList.add('active');
    
    // Reset scroll positions
    cmpScrollLeft.scrollTop = 0;
    cmpScrollRight.scrollTop = 0;
}

// 4. Close Modal
function closeCompareModal() {
    compareModal.classList.remove('active');
}

compareClose.addEventListener('click', closeCompareModal);
btnCloseCompare.addEventListener('click', closeCompareModal);

// 5. Synchronized Scrolling
// Use a flag to avoid infinite loops of scroll events
let isSyncingLeft = false;
let isSyncingRight = false;

cmpScrollLeft.addEventListener('scroll', function() {
    if (!isSyncingLeft) {
        isSyncingRight = true;
        cmpScrollRight.scrollTop = this.scrollTop;
        cmpScrollRight.scrollLeft = this.scrollLeft;
    }
    isSyncingLeft = false;
});

cmpScrollRight.addEventListener('scroll', function() {
    if (!isSyncingRight) {
        isSyncingLeft = true;
        cmpScrollLeft.scrollTop = this.scrollTop;
        cmpScrollLeft.scrollLeft = this.scrollLeft;
    }
    isSyncingRight = false;
});

// Helper for escape HTML
if (typeof escapeHtmlCompare === 'undefined') {
    function escapeHtmlCompare(text) {
        if (!text) return '';
        return text
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
    }
}