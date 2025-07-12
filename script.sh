#!/bin/bash

# Ubuntu Mining Optimization Script
# Run as root or with sudo

echo "=== Ubuntu Mining Setup for XMRig ==="

# Update system
echo "Updating system packages..."
apt update && apt upgrade -y

# Install required packages
echo "Installing required packages..."
apt install -y build-essential cmake libuv1-dev libssl-dev libhwloc-dev wget git

# Download and compile XMRig
echo "Downloading XMRig..."
cd /opt
git clone https://github.com/xmrig/xmrig.git
cd xmrig
mkdir build
cd build

# Configure and compile
echo "Compiling XMRig..."
cmake ..
make -j$(nproc)

# Create xmrig user and directories
echo "Setting up xmrig user and directories..."
useradd -r -s /bin/false xmrig
mkdir -p /etc/xmrig
mkdir -p /var/log/xmrig
chown xmrig:xmrig /var/log/xmrig

# Copy binary
cp xmrig /usr/local/bin/
chown root:root /usr/local/bin/xmrig
chmod 755 /usr/local/bin/xmrig

# Set up huge pages
echo "Configuring huge pages..."
echo 'vm.nr_hugepages=1280' >> /etc/sysctl.conf
sysctl -w vm.nr_hugepages=1280

# Optimize CPU governor
echo "Setting CPU governor to performance..."
echo 'performance' | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Create systemd service
echo "Creating systemd service..."
cat > /etc/systemd/system/xmrig.service << EOF
[Unit]
Description=XMRig Monero Miner
After=network.target

[Service]
Type=simple
User=xmrig
Group=xmrig
ExecStart=/usr/local/bin/xmrig --config=/etc/xmrig/config.json
Restart=always
RestartSec=10
Nice=-5
IOSchedulingClass=2
IOSchedulingPriority=4

[Install]
WantedBy=multi-user.target
EOF

# Set CPU affinity and priority optimizations
echo "Setting up CPU optimizations..."
echo 'kernel.sched_min_granularity_ns = 10000000' >> /etc/sysctl.conf
echo 'kernel.sched_wakeup_granularity_ns = 15000000' >> /etc/sysctl.conf
echo 'kernel.sched_migration_cost_ns = 5000000' >> /etc/sysctl.conf

# Disable CPU mitigations for better performance (optional, reduces security)
echo "Adding CPU optimization boot parameters..."
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="[^"]*/& mitigations=off processor.max_cstate=1 intel_idle.max_cstate=0/' /etc/default/grub
update-grub

# Create monitoring script
cat > /usr/local/bin/mining-monitor.sh << 'EOF'
#!/bin/bash
# Mining monitoring script

while true; do
    echo "=== Mining Status $(date) ==="
    systemctl status xmrig --no-pager -l
    echo ""
    echo "=== System Resources ==="
    free -h
    echo ""
    echo "=== CPU Usage ==="
    top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//'
    echo ""
    echo "=== Temperature ==="
    sensors 2>/dev/null | grep -E "(Core|Package)" || echo "lm-sensors not installed"
    echo ""
    sleep 300  # Check every 5 minutes
done
EOF

chmod +x /usr/local/bin/mining-monitor.sh

# Install monitoring tools
echo "Installing monitoring tools..."
apt install -y htop iotop lm-sensors
sensors-detect --auto

# Enable and start services
echo "Enabling services..."
systemctl daemon-reload
systemctl enable xmrig

echo ""
echo "=== Setup Complete ==="
echo "1. Copy your config.json to /etc/xmrig/config.json"
echo "2. Edit the config file with your wallet address"
echo "3. Start mining with: systemctl start xmrig"
echo "4. Monitor with: systemctl status xmrig"
echo "5. View logs with: journalctl -u xmrig -f"
echo "6. Run monitoring script: /usr/local/bin/mining-monitor.sh"
echo ""
echo "Remember to reboot after setup for all optimizations to take effect!"
