How to NPM unistall unused packages in Node.js

Consider, you’re developing a shiny new node.js project and according to your need, design, and business logic, you’ve installed many NPM packages, but at the end you realized, that you don’t need many packages, so now you want to remove all unused npm packages and sub-modules of it, which is not defined in **package.json**, for that purpose **npm cli**, provides a method, which we will discuss and learn how to use it in this node.js how to tutorial, so let’s learn **npm command to uninstall unused packages in Node.js.**

1.  npm prune — production

This npm command can be used to remove not required packages from your node\_modules directory and **devDependencies** modules if **NODE\_ENV** environment variable is set to **production** and if you don’t want remove **devDependencies** then you need to set — production=false

Now let’s see, how to use npm prune with example:

steps by step procedure to use [npm prune](https://docs.npmjs.com/cli/prune):

1.  First, remove the npm packages from **packages.json** file and save the file.
2.  To remove any specific node package run the command npm prune
3.  run the npm prune command to remove unused or not required node packages from Node.js
4.  if you want to remove **devDependencies** then run prune command with **–production** flag npm prune — production=true
5.  if you don’t want to unbuild **devDependencies** then you need to set **–production** flag **false** npm prune — production=false

> *If you see an npm module remain in your* **node\_modules** *directory even after running* **npm prune** *even though it’s not in* **package.json**\_, then you need to check your\_ **npm-shrinkwrap.json** *if it’s present then you need to delete it and then You can follow below method to solve this problem.*

[**READ** Get List of all files in a directory in Node.js](https://stackfame.com/list-all-files-in-a-directory-nodejs)

If you want to completely remove the ***node\_modules*** directory and want to do a fresh ***npm install***then this below one-line can be very useful:

1.  rm -rf node\_modules && npm install

That was quick!

But, This can take some time depending upon the size of the ***node\_modules*** directory.

If you have any queries, please comment below and thanks for reading this how-to guide.

first appeared on [StackFAME](https://stackfame.com/npm-unistall-unused-packages-node-js).
