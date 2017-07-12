# -*- mode: ruby -*-
# vi: set ft=ruby :

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure("2") do |config|
  # The most common configuration options are documented and commented below.
  # For a complete reference, please see the online documentation at
  # https://docs.vagrantup.com.

  # Every Vagrant development environment requires a box. You can search for
  # boxes at https://atlas.hashicorp.com/search.
  config.vm.box = "ubuntu/trusty64"

  # Disable automatic box update checking. If you disable this, then
  # boxes will only be checked for updates when the user runs
  # `vagrant box outdated`. This is not recommended.
  # config.vm.box_check_update = false

  # Create a forwarded port mapping which allows access to a specific port
  # within the machine from a port on the host machine. In the example below,
  # accessing "localhost:8080" will access port 80 on the guest machine.
  # config.vm.network "forwarded_port", guest: 80, host: 8080

  # Create a private network, which allows host-only access to the machine
  # using a specific IP.
  # config.vm.network "private_network", ip: "192.168.33.10"

  # Create a public network, which generally matched to bridged network.
  # Bridged networks make the machine appear as another physical device on
  # your network.
  # config.vm.network "public_network"

  # Share an additional folder to the guest VM. The first argument is
  # the path on the host to the actual folder. The second argument is
  # the path on the guest to mount the folder. And the optional third
  # argument is a set of non-required options.
  # config.vm.synced_folder "../data", "/vagrant_data"

  # Provider-specific configuration so you can fine-tune various
  # backing providers for Vagrant. These expose provider-specific options.
  # Example for VirtualBox:
  #
  # config.vm.provider "virtualbox" do |vb|
  #   # Display the VirtualBox GUI when booting the machine
  #   vb.gui = true
  #
  #   # Customize the amount of memory on the VM:
  #   vb.memory = "1024"
  # end
  #
  # View the documentation for the provider you are using for more
  # information on available options.

  # Define a Vagrant Push strategy for pushing to Atlas. Other push strategies
  # such as FTP and Heroku are also available. See the documentation at
  # https://docs.vagrantup.com/v2/push/atlas.html for more information.
  # config.push.define "atlas" do |push|
  #   push.app = "YOUR_ATLAS_USERNAME/YOUR_APPLICATION_NAME"
  # end

  # Enable provisioning with a shell script. Additional provisioners such as
  # Puppet, Chef, Ansible, Salt, and Docker are also available. Please see the
  # documentation for more information about their specific syntax and use.
  # config.vm.provision "shell", inline: <<-SHELL
  #   apt-get update
  #   apt-get install -y apache2
  # SHELL

  config.vm.provider "virtualbox" do |vb|
    vb.memory = "1024"
  end

  config.vm.provision "shell", inline: <<-SHELL
    apt-get install -y aptitude && 
    aptitude update && 
    aptitude safe-upgrade -y && 
    aptitude dist-upgrade -y && 
    aptitude full-upgrade -y && 
    aptitude purge ~c -y && 
    aptitude clean
    export DEBIAN_FRONTEND="noninteractive"
    echo sudo debconf-set-selections <<< "mysql-server mysql-server/root_password password ''"
    echo sudo debconf-set-selections <<< "mysql-server mysql-server/root_password_again password ''"
    apt-get install -y gcc make libmysqlclient-dev mysql-server
    wget --quiet https://github.com/antirez/redis/archive/4.0-rc3.tar.gz
    tar xzf 4.0-rc3.tar.gz
    rm 4.0-rc3.tar.gz
    cd redis-4.0-rc3
    make
    sudo make install
    cd utils
    sudo ./install_server.sh
    cd ../..
    rm -Rf redis-4.0-rc3
    cd /vagrant/src/scache
    make clean
    make
    echo "create database redisdb;" | mysql -u root
    echo "create user redisuser@'%' identified by 'redispassword';" | mysql -u root
    echo "grant all privileges on redisdb.* to redisuser;" | mysql -u root
    echo "create table redisdb.customer (id integer, Nom varchar(255), Prenom varchar(255), DateDeNaissance datetime);" | mysql -u root
    echo 'insert into redisdb.customer values (1,"Cerbelle","François","2017-05-26");' | mysql -u root
    echo 'insert into redisdb.customer values (2,"Carbonnel","Georges","1970-01-01");' | mysql -u root
    echo 'insert into redisdb.customer values (3,"Sanfilippo","Salvatore","1970-01-01");' | mysql -u root
    sudo /etc/init.d/redis_6379 start
    redis-cli module load /vagrant/src/scache/scache.so
    redis-cli scache.list
    redis-cli scache.create cache1 20 127.0.0.1 3306 redisdb redisuser redispassword
    redis-cli scache.create cache2 10 localhost 3306 redisdb redisuser redispassword
    redis-cli scache.create cache3 5 nodeX.vm 3306 redisdb redisuser redispassword
    redis-cli scache.create cache4 5 localhost 3306 redisdb redisuser redispassword
    redis-cli scache.list
    redis-cli scache.info cache2
    redis-cli scache.delete cache2
    redis-cli scache.list
    redis-cli scache.delete nonexisting
    redis-cli scache.delete cache3
    redis-cli scache.delete nonexisting
    time redis-cli scache.getvalue cache1 'select * from customer'
    time redis-cli scache.getvalue cache1 'select * from customer'
  SHELL
end
