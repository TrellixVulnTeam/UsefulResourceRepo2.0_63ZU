import React from 'react';
import ReactDOM from 'react-dom';
import App from './components/App/app';
import './styles/main.sass';
import { FavoriteCourses } from './components/FavoriteCourses/fav_courses';
import registerServiceWorker from './registerServiceWorker';
import { BrowserRouter, Switch, Route } from 'react-router-dom';


ReactDOM.render((
 <BrowserRouter>
    <Switch>
      <Route exact path="/" component={App} />
      <Route exact path="/favorites" component={FavoriteCourses} />
    </Switch>
  </BrowserRouter> 
), document.getElementById('root'));
registerServiceWorker();
