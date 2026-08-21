# LD7 Provisioning Draft

这是一个只读的 Ansible（配置编排工具）check-mode 草案，不是 Orange Pi 部署入口。

它只验证：

- 目标是 Linux；
- 调用方明确提供已经生成的 release artifact；
- artifact 是存在的普通文件。

它不安装包、不创建用户、不写 `/opt/robot-control-runtime`、不启停 systemd、不打开 CAN/串口，
也不读取 Orange Pi inventory。正式安装和回滚仍由现有
[`deploy/orangepi/install_release.sh`](../orangepi/install_release.sh)、
[`rcr_operations.sh`](../orangepi/rcr_operations.sh) 和 systemd unit 合同负责。

## 本地验证

有 Ansible 时：

```bash
RCR_ARTIFACT=/tmp/rcr-release.tar.gz \
  ansible-playbook --check --diff \
  -i deploy/provisioning/inventory.example.yml \
  deploy/provisioning/check.yml
```

CI/开发机没有 `ansible-playbook` 时，LD7 将结果记录为
`provisioning=unsupported reason=ansible-playbook-not-installed`，不把未执行写成 PASS：

```bash
linux/scripts/ci/check_provisioning.sh
```

Orange Pi inventory、实际 apply、权限、服务启动和物理设备验证留给后置 Gate。
